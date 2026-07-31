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
        """Locate the emu-main-next crashreport binary in Bazel runfiles, installed release dir, or workspace bazel-bin."""
        exe_suffix = ".exe" if sys.platform == "win32" else ""

        # 1. Search Bazel runfiles (when executed inside Bazel or compiled advisor binary with .runfiles)
        candidates = [
            f"goldfish+/emulator/crashreport/tool/crashreport{exe_suffix}",
            f"goldfish/emulator/crashreport/tool/crashreport{exe_suffix}",
            f"emulator/crashreport/tool/crashreport{exe_suffix}",
        ]
        if self.runfiles:
            for candidate in candidates:
                path = self.runfiles.Rlocation(candidate)
                if path and Path(path).exists():
                    logging.debug("Found crashreport binary via Bazel runfiles at: %s", path)
                    return path

        # 2. Search adjacent installed release binary directory (~/.android/emu-dev-cli/lib/bin/crashreport)
        try:
            exe_dir = Path(sys.executable).parent
            adjacent_installed = exe_dir / f"crashreport{exe_suffix}"
            if adjacent_installed.exists() and os.access(adjacent_installed, os.X_OK):
                logging.debug("Found crashreport binary adjacent to executable at: %s", adjacent_installed)
                return str(adjacent_installed)

            home_installed = Path.home() / ".android" / "emu-dev-cli" / "lib" / "bin" / f"crashreport{exe_suffix}"
            if home_installed.exists() and os.access(home_installed, os.X_OK):
                logging.debug("Found crashreport binary at installed release location: %s", home_installed)
                return str(home_installed)
        except Exception:
            pass

        # 3. Search Bazel workspace build output
        try:
            cwd_path = Path.cwd()
            for p in [cwd_path] + list(cwd_path.parents):
                bbin = p / "bazel-bin" / "external" / "goldfish+" / "emulator" / "crashreport" / "tool" / f"crashreport{exe_suffix}"
                if bbin.exists() and os.access(bbin, os.X_OK):
                    logging.debug("Found crashreport binary at workspace build output: %s", bbin)
                    return str(bbin.resolve())
        except Exception:
            pass

        logging.error("Failed to locate emu-main-next built crashreport binary.")
        return None

    def generate_dump(self, context: CrashReportContext, symbols_dir: Path) -> Path:
        """Invoke crashreport binary to generate text (stack + breadcrumbs) local dump."""
        txt_path = context.work_dir / "crashreport.txt"
        if not self.crashreport_path:
            raise FileNotFoundError("crashreport binary not available.")

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
                process = subprocess.run(
                    txt_cmd,
                    stdout=out_f,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=False,
                )
            if process.returncode != 0:
                logging.warning(
                    "crashreport binary (text) returned non-zero exit code %d: %s",
                    process.returncode,
                    process.stderr,
                )
            logging.info(
                "Successfully generated human-readable local dump at: %s", txt_path
            )
            return txt_path
        except Exception as e:
            logging.error("Failed to execute crashreport binary: %s", e)
            raise
