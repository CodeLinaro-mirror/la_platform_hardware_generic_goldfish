# -*- coding: utf-8 -*-
# Copyright 2026 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Cross-platform process tree monitoring and telemetry library."""

import asyncio
import collections
import dataclasses
import logging
import time
from typing import Dict, List, Optional, Set, Tuple

import psutil


@dataclasses.dataclass
class ProcessSample:
    """A point-in-time telemetry sample for a process and its child hierarchy."""

    timestamp: float
    total_cpu_percent: float
    parent_cpu_percent: float
    children_cpu_percent: float
    rss_bytes: int
    process_count: int


@dataclasses.dataclass
class ProcessMetricsSummary:
    """Aggregated telemetry metrics collected over a monitoring interval."""

    duration_sec: float
    sample_count: int
    avg_cpu_percent: float
    peak_cpu_percent: float
    peak_rss_bytes: int
    recent_cpu_samples: List[float] = dataclasses.field(default_factory=list)
    process_details: List[Dict[str, object]] = dataclasses.field(default_factory=list)


class ProcessTreeMonitor:
    """Monitors a process and its child subtree across Linux, macOS, and Windows.

    Provides periodic non-blocking CPU and memory sampling, historical telemetry,
    and structured process tree inspection on timeout / exit.
    """

    def __init__(
        self,
        pid: int,
        sample_interval_sec: float = 1.0,
        history_len: int = 10,
    ):
        self.pid = pid
        self.sample_interval_sec = sample_interval_sec
        self.history_len = history_len

        self._parent_proc: Optional[psutil.Process] = None
        try:
            self._parent_proc = psutil.Process(pid)
            # Prime initial CPU baseline
            self._parent_proc.cpu_percent(interval=None)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            self._parent_proc = None

        self._child_procs: Dict[int, psutil.Process] = {}
        self._history_procs: Dict[int, Dict[str, object]] = {}

        # O(1) running accumulators to prevent unbounded memory growth
        self._sample_count: int = 0
        self._cpu_percent_sum: float = 0.0
        self._peak_cpu_percent: float = 0.0
        self._peak_rss_bytes: int = 0
        self._recent_cpu_samples: collections.deque[float] = collections.deque(
            maxlen=history_len
        )

        self._start_time: float = time.time()
        self._stop_time: Optional[float] = None
        self._task: Optional[asyncio.Task] = None
        self._running: bool = False

    async def start(self) -> None:
        """Starts asynchronous background sampling."""
        if self._running:
            return
        self._running = True
        self._start_time = time.time()
        self._task = asyncio.create_task(self._sample_loop())

    async def stop(self) -> ProcessMetricsSummary:
        """Stops background sampling and returns aggregated summary metrics."""
        if not self._running and self._stop_time is not None:
            return self.get_summary()

        self._running = False
        if self._task and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        self._stop_time = time.time()
        return self.get_summary()

    async def _sample_loop(self) -> None:
        """Internal background async sampling loop."""
        while self._running:
            await asyncio.sleep(self.sample_interval_sec)
            if not self._running:
                break
            try:
                self.sample_once()
            except Exception as e:
                logging.debug("Error during process monitoring sample: %s", e)

    def sample_once(self) -> Optional[ProcessSample]:
        """Takes a synchronous point-in-time snapshot of the process tree."""
        parent_metrics = self._sample_parent()
        if parent_metrics is None:
            # Sync remaining child processes so terminated children are evicted and marked in history
            self._sync_child_processes()
            return None
        parent_cpu, parent_rss = parent_metrics

        new_children = self._sync_child_processes()
        children_cpu, children_rss = self._sample_children(new_children)

        total_cpu = parent_cpu + children_cpu
        total_rss = parent_rss + children_rss
        process_count = 1 + len(self._child_procs)

        self._record_sample(total_cpu, total_rss)

        return ProcessSample(
            timestamp=time.time(),
            total_cpu_percent=total_cpu,
            parent_cpu_percent=parent_cpu,
            children_cpu_percent=children_cpu,
            rss_bytes=total_rss,
            process_count=process_count,
        )

    def _update_proc_history(self, proc: psutil.Process) -> None:
        """Updates last-known metadata for a tracked process."""
        try:
            if proc.is_running():
                rss_mb = round(proc.memory_info().rss / (1024 * 1024), 1)
                existing = self._history_procs.get(proc.pid)
                peak_rss = max(rss_mb, existing["peak_rss_mb"]) if existing else rss_mb
                self._history_procs[proc.pid] = {
                    "pid": proc.pid,
                    "name": proc.name(),
                    "status": proc.status(),
                    "num_threads": proc.num_threads(),
                    "rss_mb": rss_mb,
                    "peak_rss_mb": peak_rss,
                }
            else:
                if proc.pid in self._history_procs:
                    self._history_procs[proc.pid]["status"] = "terminated"
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            if proc.pid in self._history_procs:
                self._history_procs[proc.pid]["status"] = "terminated"

    def _sample_parent(self) -> Optional[Tuple[float, int]]:
        """Samples CPU and RSS of the parent process, returning None if dead or inaccessible."""
        is_new_parent = False
        if not self._parent_proc:
            try:
                self._parent_proc = psutil.Process(self.pid)
                # Prime initial timestamp baseline; first call returns meaningless 0.0
                self._parent_proc.cpu_percent(interval=None)
                self._update_proc_history(self._parent_proc)
                is_new_parent = True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                return None
        elif not self._parent_proc.is_running():
            if self.pid in self._history_procs:
                self._history_procs[self.pid]["status"] = "terminated"
            return None

        try:
            # If parent was just discovered on this tick, exclude CPU measurement (0.0)
            # until the next sampling tick when a real time delta has elapsed.
            if is_new_parent:
                parent_cpu = 0.0
            else:
                parent_cpu = self._parent_proc.cpu_percent(interval=None)
            parent_rss = self._parent_proc.memory_info().rss
            self._update_proc_history(self._parent_proc)
            return parent_cpu, parent_rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            if self.pid in self._history_procs:
                self._history_procs[self.pid]["status"] = "terminated"
            return None

    def _sync_child_processes(self) -> Set[int]:
        """Discovers new child processes, primes their baseline, and evicts dead children."""
        current_children = []
        active_pids = set()
        if self._parent_proc:
            try:
                if self._parent_proc.is_running():
                    current_children = self._parent_proc.children(recursive=True)
                    active_pids = {c.pid for c in current_children if c.is_running()}
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                current_children = []
                active_pids = set()

        # Evict terminated children and mark status in history
        for child_pid in list(self._child_procs.keys()):
            if child_pid not in active_pids:
                del self._child_procs[child_pid]
                if child_pid in self._history_procs:
                    self._history_procs[child_pid]["status"] = "terminated"

        # Add newly discovered children and prime their initial CPU baseline
        new_children: Set[int] = set()
        for child in current_children:
            if child.pid not in self._child_procs and child.is_running():
                try:
                    # Prime initial timestamp baseline; first call returns meaningless 0.0
                    child.cpu_percent(interval=None)
                    self._child_procs[child.pid] = child
                    new_children.add(child.pid)
                    self._update_proc_history(child)
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    pass

        return new_children

    def _sample_children(self, new_children: Set[int]) -> Tuple[float, int]:
        """Samples CPU and RSS across active child processes, skipping new children for CPU."""
        children_cpu = 0.0
        children_rss = 0
        for child in list(self._child_procs.values()):
            try:
                if child.is_running():
                    # If this child was just discovered on this tick, its baseline was primed in _sync_child_processes.
                    # Only sample CPU on children that have existed since at least the previous tick.
                    if child.pid not in new_children:
                        children_cpu += child.cpu_percent(interval=None)
                    children_rss += child.memory_info().rss
                    self._update_proc_history(child)
                else:
                    self._child_procs.pop(child.pid, None)
                    if child.pid in self._history_procs:
                        self._history_procs[child.pid]["status"] = "terminated"
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                self._child_procs.pop(child.pid, None)
                if child.pid in self._history_procs:
                    self._history_procs[child.pid]["status"] = "terminated"

        return children_cpu, children_rss

    def _record_sample(self, total_cpu: float, total_rss: int) -> None:
        """Updates running accumulators and recent sample history."""
        self._sample_count += 1
        self._cpu_percent_sum += total_cpu
        self._peak_cpu_percent = max(self._peak_cpu_percent, total_cpu)
        self._peak_rss_bytes = max(self._peak_rss_bytes, total_rss)
        self._recent_cpu_samples.append(round(total_cpu, 1))

    def get_summary(self) -> ProcessMetricsSummary:
        """Computes summary statistics from recorded cumulative metrics."""
        duration = (self._stop_time or time.time()) - self._start_time
        if self._sample_count == 0:
            return ProcessMetricsSummary(
                duration_sec=duration,
                sample_count=0,
                avg_cpu_percent=0.0,
                peak_cpu_percent=0.0,
                peak_rss_bytes=0,
                recent_cpu_samples=[],
                process_details=self._collect_process_details(),
            )

        avg_cpu = self._cpu_percent_sum / self._sample_count

        return ProcessMetricsSummary(
            duration_sec=duration,
            sample_count=self._sample_count,
            avg_cpu_percent=avg_cpu,
            peak_cpu_percent=self._peak_cpu_percent,
            peak_rss_bytes=self._peak_rss_bytes,
            recent_cpu_samples=list(self._recent_cpu_samples),
            process_details=self._collect_process_details(),
        )

    def _collect_process_details(self) -> List[Dict[str, object]]:
        """Collects state, thread count, and memory for all tracked processes in the tree."""
        if self._parent_proc:
            self._update_proc_history(self._parent_proc)
        for child in self._child_procs.values():
            self._update_proc_history(child)

        return list(self._history_procs.values())

    def format_summary_line(
        self, summary: Optional[ProcessMetricsSummary] = None, tag: str = "Process Telemetry"
    ) -> str:
        """Generates a compact one-line log summary."""
        s = summary or self.get_summary()
        if s.sample_count == 0:
            return f"--- {tag}: PID {self.pid} exited or unreachable ---"

        peak_rss_mb = s.peak_rss_bytes / (1024 * 1024)
        return (
            f"--- {tag}: Time={s.duration_sec:.1f}s | "
            f"Avg CPU={s.avg_cpu_percent:.1f}% | "
            f"Peak CPU={s.peak_cpu_percent:.1f}% | "
            f"Peak RSS={peak_rss_mb:.1f} MB (Samples={s.sample_count}) ---"
        )

    def format_diagnostic_dump(
        self, summary: Optional[ProcessMetricsSummary] = None, reason: str = "Timeout"
    ) -> str:
        """Generates a detailed post-mortem report for triage."""
        s = summary or self.get_summary()
        peak_rss_mb = s.peak_rss_bytes / (1024 * 1024)
        lines = [
            "===================== PROCESS TELEMETRY DUMP =====================",
            f"Failure Reason: {reason}",
            f"Duration: {s.duration_sec:.1f}s | Samples Collected: {s.sample_count}",
            f"Avg CPU: {s.avg_cpu_percent:.1f}% | Peak CPU: {s.peak_cpu_percent:.1f}% | Peak RSS: {peak_rss_mb:.1f} MB",
            f"Recent CPU Samples: {s.recent_cpu_samples}",
        ]

        if s.process_details:
            lines.append("Process Tree Hierarchy:")
            for p in s.process_details:
                if p["status"] == "terminated":
                    lines.append(
                        f"  - {p['name']} (PID {p['pid']}): Status=terminated "
                        f"(Peak RSS={p['peak_rss_mb']}MB)"
                    )
                else:
                    lines.append(
                        f"  - {p['name']} (PID {p['pid']}): Status={p['status']}, "
                        f"Threads={p['num_threads']}, RSS={p['rss_mb']}MB "
                        f"(Peak RSS={p['peak_rss_mb']}MB)"
                    )
        lines.append(
            "=================================================================="
        )
        return "\n".join(lines)
