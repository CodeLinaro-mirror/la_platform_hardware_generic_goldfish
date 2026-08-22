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

"""Unit tests for ProcessTreeMonitor."""

import asyncio
import os
import subprocess
import sys
import time
import unittest

from process_monitor import (
    ProcessMetricsSummary,
    ProcessSample,
    ProcessTreeMonitor,
)


class ProcessTreeMonitorTest(unittest.IsolatedAsyncioTestCase):
    """Tests ProcessTreeMonitor functionality and telemetry formatting."""

    def test_sample_once_current_process(self):
        """Verify sampling the current running process."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.1)
        sample = monitor.sample_once()
        self.assertIsNotNone(sample)
        self.assertIsInstance(sample, ProcessSample)
        self.assertGreaterEqual(sample.total_cpu_percent, 0.0)
        self.assertGreater(sample.rss_bytes, 0)
        self.assertGreaterEqual(sample.process_count, 1)

    async def test_async_monitoring_lifecycle(self):
        """Verify starting, sampling asynchronously, and stopping the monitor."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.05)
        await monitor.start()
        await asyncio.sleep(0.15)
        summary = await monitor.stop()

        self.assertIsInstance(summary, ProcessMetricsSummary)
        self.assertGreater(summary.sample_count, 0)
        self.assertGreater(summary.duration_sec, 0.0)
        self.assertGreater(summary.peak_rss_bytes, 0)
        self.assertIn("Process Telemetry", monitor.format_summary_line(summary))
        self.assertIn("Boot Telemetry", monitor.format_summary_line(summary, tag="Boot Telemetry"))

    async def test_process_tree_child_discovery_and_reuse(self):
        """Verify that spawned child processes are discovered, primed, and reused."""
        cmd = [sys.executable, "-c", "import time; time.sleep(1.0)"]
        proc = subprocess.Popen(cmd)
        try:
            monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.05)
            # First tick: discovers and primes child
            sample1 = monitor.sample_once()
            self.assertIsNotNone(sample1)
            self.assertIn(proc.pid, monitor._child_procs)
            child_instance = monitor._child_procs[proc.pid]

            # Second tick: reuses same psutil.Process instance
            sample2 = monitor.sample_once()
            self.assertIsNotNone(sample2)
            self.assertIs(monitor._child_procs[proc.pid], child_instance)
        finally:
            proc.kill()
            proc.wait()

    def test_child_priming_and_first_sample_delay(self):
        """Verify new child is primed on discovery tick and reports real CPU on next tick."""
        # Run a busy child that spins for 2 seconds to burn CPU
        cmd = [
            sys.executable,
            "-c",
            "import time; end = time.time() + 2.0; [None for _ in iter(lambda: time.time() > end, True)]",
        ]
        proc = subprocess.Popen(cmd)
        try:
            monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.1)

            # Tick 1: Discovery tick. The child is newly discovered.
            # Its baseline must be primed, and it must be excluded from children_cpu_percent
            # on this tick to avoid reporting an invalid / garbage 0.0 value into stats.
            sample1 = monitor.sample_once()
            self.assertIsNotNone(sample1)
            self.assertIn(proc.pid, monitor._child_procs)
            self.assertEqual(sample1.children_cpu_percent, 0.0)

            # Let the child burn CPU for 150ms
            time.sleep(0.15)

            # Tick 2: Subsequent tick with same persistent child psutil.Process instance.
            # It should now measure the CPU consumed between Tick 1 and Tick 2 (> 0.0%).
            sample2 = monitor.sample_once()
            self.assertIsNotNone(sample2)
            self.assertGreater(sample2.children_cpu_percent, 0.0)
        finally:
            proc.kill()
            proc.wait()

    async def test_sample_loop_initial_delay(self):
        """Verify that _sample_loop waits sample_interval_sec before taking the first sample."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.2)
        await monitor.start()
        # Immediately after start (t < sample_interval_sec), no samples should be recorded yet
        # because the loop sleeps sample_interval_sec first to allow CPU time delta to elapse.
        await asyncio.sleep(0.05)
        self.assertEqual(monitor._sample_count, 0)

        # After sample_interval_sec has elapsed, sample 1 should be collected.
        await asyncio.sleep(0.25)
        self.assertGreaterEqual(monitor._sample_count, 1)
        await monitor.stop()

    def test_summary_calculation(self):
        """Verify mathematical calculation of summary metrics using accumulators."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=1.0)
        # Directly configure running accumulators
        monitor._sample_count = 3
        monitor._cpu_percent_sum = 600.0
        monitor._peak_cpu_percent = 300.0
        monitor._peak_rss_bytes = 200 * 1024 * 1024
        monitor._recent_cpu_samples.extend([100.0, 200.0, 300.0])

        summary = monitor.get_summary()
        self.assertEqual(summary.sample_count, 3)
        self.assertAlmostEqual(summary.avg_cpu_percent, 200.0, places=1)
        self.assertAlmostEqual(summary.peak_cpu_percent, 300.0, places=1)
        self.assertEqual(summary.peak_rss_bytes, 200 * 1024 * 1024)
        self.assertEqual(summary.recent_cpu_samples, [100.0, 200.0, 300.0])

    def test_diagnostic_dump_formatting(self):
        """Verify diagnostic dump contains factual metrics and process details."""
        monitor = ProcessTreeMonitor(os.getpid())
        monitor._sample_count = 1
        monitor._cpu_percent_sum = 45.5
        monitor._peak_cpu_percent = 45.5
        monitor._peak_rss_bytes = 120 * 1024 * 1024
        monitor._recent_cpu_samples.append(45.5)

        summary = monitor.get_summary()
        dump = monitor.format_diagnostic_dump(summary, reason="Timeout (180s)")
        self.assertIn("PROCESS TELEMETRY DUMP", dump)
        self.assertIn("Failure Reason: Timeout (180s)", dump)
        self.assertIn("Recent CPU Samples: [45.5]", dump)

    async def test_stop_idempotency(self):
        """Verify calling stop() multiple times does not inflate duration or overwrite stop_time."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.05)
        await monitor.start()
        await asyncio.sleep(0.1)
        summary1 = await monitor.stop()
        stop_time1 = monitor._stop_time

        # Sleep further before calling stop() a second time
        await asyncio.sleep(0.15)
        summary2 = await monitor.stop()

        self.assertEqual(monitor._stop_time, stop_time1)
        self.assertEqual(summary1.duration_sec, summary2.duration_sec)
        self.assertEqual(summary1.sample_count, summary2.sample_count)

    def test_parent_process_termination_no_pid_reuse(self):
        """Verify that when parent process exits, sample_once returns None without resurrecting."""
        proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.05)"])
        monitor = ProcessTreeMonitor(proc.pid, sample_interval_sec=0.05)
        self.assertIsNotNone(monitor._parent_proc)

        # Wait for the process to exit
        proc.wait()

        # sample_once should see parent is not running and return None without re-instantiating Process
        sample = monitor.sample_once()
        self.assertIsNone(sample)

    def test_handling_dead_process(self):
        """Verify that a terminated PID is handled gracefully."""
        invalid_pid = 99999999
        monitor = ProcessTreeMonitor(invalid_pid)
        sample = monitor.sample_once()
        self.assertIsNone(sample)
        summary = monitor.get_summary()
        self.assertEqual(summary.sample_count, 0)
        self.assertIn(
            "PID 99999999 exited or unreachable", monitor.format_summary_line(summary)
        )

    def test_sample_parent_helper(self):
        """Verify _sample_parent returns metrics for running process and None for dead process."""
        monitor = ProcessTreeMonitor(os.getpid())
        metrics = monitor._sample_parent()
        self.assertIsNotNone(metrics)
        cpu, rss = metrics
        self.assertGreaterEqual(cpu, 0.0)
        self.assertGreater(rss, 0)

        # Dead process returns None
        dead_monitor = ProcessTreeMonitor(99999999)
        self.assertIsNone(dead_monitor._sample_parent())

    def test_sync_child_processes_helper(self):
        """Verify _sync_child_processes tracks new children and evicts dead children."""
        proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(1.0)"])
        try:
            monitor = ProcessTreeMonitor(os.getpid())
            # First sync: discovers proc.pid as a new child
            new_children = monitor._sync_child_processes()
            self.assertIn(proc.pid, new_children)
            self.assertIn(proc.pid, monitor._child_procs)

            # Second sync while still alive: no new children returned
            new_children_2 = monitor._sync_child_processes()
            self.assertNotIn(proc.pid, new_children_2)
            self.assertIn(proc.pid, monitor._child_procs)
        finally:
            proc.kill()
            proc.wait()

        # Third sync after termination: proc.pid evicted from _child_procs
        monitor._sync_child_processes()
        self.assertNotIn(proc.pid, monitor._child_procs)

    def test_sample_children_helper(self):
        """Verify _sample_children skips new_children for CPU and measures established children."""
        cmd = [
            sys.executable,
            "-c",
            "import time; end = time.time() + 2.0; [None for _ in iter(lambda: time.time() > end, True)]",
        ]
        proc = subprocess.Popen(cmd)
        try:
            monitor = ProcessTreeMonitor(os.getpid())
            new_children = monitor._sync_child_processes()
            self.assertIn(proc.pid, new_children)

            # Sampling with new_children passed: CPU is 0.0 (excluded for priming)
            cpu, rss = monitor._sample_children(new_children)
            self.assertEqual(cpu, 0.0)
            self.assertGreater(rss, 0)

            # Allow child to run and burn CPU
            time.sleep(0.15)

            # Sampling with empty new_children: child is established, CPU measured > 0.0
            cpu2, rss2 = monitor._sample_children(set())
            self.assertGreater(cpu2, 0.0)
            self.assertGreater(rss2, 0)
        finally:
            proc.kill()
            proc.wait()

    def test_record_sample_helper(self):
        """Verify _record_sample updates running accumulators and recent samples deque."""
        monitor = ProcessTreeMonitor(os.getpid(), history_len=3)
        self.assertEqual(monitor._sample_count, 0)

        monitor._record_sample(total_cpu=50.0, total_rss=1024 * 1024)
        monitor._record_sample(total_cpu=150.0, total_rss=2048 * 1024)

        self.assertEqual(monitor._sample_count, 2)
        self.assertAlmostEqual(monitor._cpu_percent_sum, 200.0)
        self.assertAlmostEqual(monitor._peak_cpu_percent, 150.0)
        self.assertEqual(monitor._peak_rss_bytes, 2048 * 1024)
        self.assertEqual(list(monitor._recent_cpu_samples), [50.0, 150.0])

        # Test circular buffer overflow behavior (history_len=3)
        monitor._record_sample(total_cpu=75.0, total_rss=1500 * 1024)
        monitor._record_sample(total_cpu=80.0, total_rss=1600 * 1024)
        self.assertEqual(list(monitor._recent_cpu_samples), [150.0, 75.0, 80.0])

    def test_terminated_child_retained_in_diagnostic_dump(self):
        """Verify terminated child processes remain visible in diagnostic dump as Status=terminated."""
        proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(0.05)"])
        try:
            monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.05)
            # Sample while child is alive -> child tracked in history
            sample1 = monitor.sample_once()
            self.assertIsNotNone(sample1)
            self.assertIn(proc.pid, monitor._history_procs)
            self.assertNotEqual(monitor._history_procs[proc.pid]["status"], "terminated")

            # Wait for child to terminate
            proc.wait()

            # Next sample -> child evicted from _child_procs, marked terminated in history
            sample2 = monitor.sample_once()
            self.assertIsNotNone(sample2)
            self.assertNotIn(proc.pid, monitor._child_procs)
            self.assertEqual(monitor._history_procs[proc.pid]["status"], "terminated")

            # Diagnostic dump shows Status=terminated
            dump = monitor.format_diagnostic_dump(reason="Child Exited Test")
            self.assertIn(f"(PID {proc.pid}): Status=terminated", dump)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()

    def test_parent_death_cleans_up_and_terminates_children_in_history(self):
        """Verify that when parent process dies, children are evicted and marked terminated."""
        # Parent process that spawns a sleep child
        code = (
            "import subprocess, sys, time; "
            "p = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5.0)']); "
            "time.sleep(5.0)"
        )
        parent = subprocess.Popen([sys.executable, "-c", code])
        try:
            # Allow parent to spawn child
            time.sleep(0.2)
            monitor = ProcessTreeMonitor(parent.pid, sample_interval_sec=0.05)
            sample1 = monitor.sample_once()
            self.assertIsNotNone(sample1)
            self.assertEqual(len(monitor._child_procs), 1)
            child_pid = list(monitor._child_procs.keys())[0]
            self.assertIn(child_pid, monitor._history_procs)
            self.assertNotEqual(monitor._history_procs[child_pid]["status"], "terminated")

            # Kill parent
            parent.kill()
            parent.wait()

            # Next sample -> parent is dead, sample_once returns None, child evicted and marked terminated
            sample2 = monitor.sample_once()
            self.assertIsNone(sample2)
            self.assertNotIn(child_pid, monitor._child_procs)
            self.assertEqual(monitor._history_procs[child_pid]["status"], "terminated")
            self.assertEqual(monitor._history_procs[parent.pid]["status"], "terminated")
        finally:
            if parent.poll() is None:
                parent.kill()
                parent.wait()

    def test_lazy_parent_discovery_cpu_priming(self):
        """Verify that lazily initialized parent proc is primed on tick 0 (0.0%) and measured on tick 1."""
        monitor = ProcessTreeMonitor(os.getpid(), sample_interval_sec=0.1)
        # Simulate lazy parent discovery by resetting _parent_proc
        monitor._parent_proc = None

        # Discovery tick in _sample_parent(): should prime baseline and report 0.0% CPU
        metrics1 = monitor._sample_parent()
        self.assertIsNotNone(metrics1)
        parent_cpu1, parent_rss1 = metrics1
        self.assertEqual(parent_cpu1, 0.0)
        self.assertGreater(parent_rss1, 0)

        # Burn CPU for 100ms
        end = time.time() + 0.1
        while time.time() < end:
            pass

        # Subsequent tick in _sample_parent(): should measure real CPU (> 0.0%)
        metrics2 = monitor._sample_parent()
        self.assertIsNotNone(metrics2)
        parent_cpu2, parent_rss2 = metrics2
        self.assertGreater(parent_cpu2, 0.0)


if __name__ == "__main__":
    unittest.main()
