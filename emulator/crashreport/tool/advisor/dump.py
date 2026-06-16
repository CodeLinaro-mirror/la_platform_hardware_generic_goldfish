# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Executes the crashreport C++ binary to generate a complete local dump."""

import logging
import subprocess
import sys
from pathlib import Path
from typing import Optional

from context import CrashReportContext
from python.runfiles import Runfiles


class CrashReportDumper:
    """Executes the crashreport C++ binary to generate a complete local dump."""

    def __init__(self) -> None:
        self.runfiles = Runfiles.Create()
        if not self.runfiles:
            raise RuntimeError("Failed to initialize Bazel runfiles.")
        self.crashreport_path = self._locate_binary()

    def _locate_binary(self) -> Optional[str]:
        """Locate the crashreport binary in Bazel runfiles."""
        # Note on candidate list:
        # 'goldfish+' is the Bzlmod mangled repository name for the goldfish repository in modern Bazel.
        # 'goldfish' is the legacy WORKSPACE repository name or non-mangled apparent name.
        # 'emulator/...' covers cases where crashreport is run within the root workspace.
        exe_suffix = ".exe" if sys.platform == "win32" else ""
        candidates = [
            f"goldfish+/emulator/crashreport/tool/crashreport{exe_suffix}",
            f"goldfish/emulator/crashreport/tool/crashreport{exe_suffix}",
            f"emulator/crashreport/tool/crashreport{exe_suffix}",
        ]
        for candidate in candidates:
            path = self.runfiles.Rlocation(candidate)
            if path and Path(path).exists():
                logging.debug("Found crashreport binary at: %s", path)
                return path
        logging.error("Failed to locate crashreport binary in Bazel runfiles.")
        return None

    def generate_dump(self, context: CrashReportContext, symbols_dir: Path) -> Path:
        """Invoke crashreport binary to generate both text (stack) and JSON (machine) local dumps."""
        txt_path = context.work_dir / "crashreport.txt"
        json_path = context.work_dir / "crashreport.json"
        if not self.crashreport_path:
            raise FileNotFoundError("crashreport binary not available.")

        # 1. Generate Human-Readable Text Dump (Summary/Stack + Breadcrumbs)
        txt_cmd = [
            self.crashreport_path,
            f"--minidump={context.minidump_path}",
            f"--symbol_paths={symbols_dir}",
        ]
        logging.info(
            "Executing crashreport binary to generate human-readable text dump..."
        )
        logging.debug("Command: %s", " ".join(txt_cmd))

        txt_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with open(txt_path, "w", encoding="utf-8") as out_f:
                process1 = subprocess.run(
                    txt_cmd,
                    stdout=out_f,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=False,
                )
            if process1.returncode != 0:
                logging.warning(
                    "crashreport binary (text) returned non-zero exit code %d: %s",
                    process1.returncode,
                    process1.stderr,
                )
            logging.info(
                "Successfully generated human-readable local dump at: %s", txt_path
            )

            # 2. Generate Machine-Readable JSON Dump (--format=machine)
            json_cmd = [
                self.crashreport_path,
                f"--minidump={context.minidump_path}",
                f"--symbol_paths={symbols_dir}",
                "--format=machine",
            ]
            logging.info(
                "Executing crashreport binary to generate machine-readable JSON dump..."
            )
            logging.debug("Command: %s", " ".join(json_cmd))

            with open(json_path, "w", encoding="utf-8") as out_f:
                process2 = subprocess.run(
                    json_cmd,
                    stdout=out_f,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=False,
                )
            if process2.returncode != 0:
                logging.warning(
                    "crashreport binary (json) returned non-zero exit code %d: %s",
                    process2.returncode,
                    process2.stderr,
                )
            logging.info(
                "Successfully generated machine-readable JSON dump at: %s", json_path
            )

            return txt_path
        except Exception as e:
            logging.error("Failed to execute crashreport binary: %s", e)
            raise
