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

"""Manages the crash identifier and working directory sandbox."""

import getpass
import logging
import re
import tempfile
from pathlib import Path
from typing import Optional


class CrashReportContext:
    """Manages the crash identifier and working directory sandbox."""

    def __init__(self, input_str: str, base_dir: Optional[str] = None, aosp_root: Optional[str] = None, branch: Optional[str] = None, build_id: Optional[str] = None) -> None:
        self.raw_input = input_str
        self.aosp_root = aosp_root
        self.branch = branch
        self.build_id = build_id
        self.crash_id = self._parse_crash_id(input_str)
        if not base_dir:
            user = getpass.getuser()
            base_dir = f"{tempfile.gettempdir()}/crashadvisor_{user}"
        self.work_dir = Path(base_dir) / self.crash_id
        self.metadata_path = self.work_dir / "metadata.json"
        self.minidump_path = self.work_dir / "minidump.dmp"

    @staticmethod
    def _parse_crash_id(input_str: str) -> str:
        """Extract the raw crash ID from a raw ID or go/crash URL."""
        match = re.search(r"([a-fA-F0-9]{16})", input_str)
        if match:
            return match.group(1)
        return input_str

    def prepare_sandbox(self) -> None:
        """Create the working directory sandbox if it does not exist."""
        logging.debug("Preparing sandbox directory: %s", self.work_dir)
        self.work_dir.mkdir(parents=True, exist_ok=True)
