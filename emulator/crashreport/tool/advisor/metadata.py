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

"""Parses and exposes structural data from the report_proto JSON object."""

import json
import logging
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


class CrashMetadata:
    """Parses and exposes structural data from the report_proto JSON object."""

    def __init__(self, metadata_path: Path) -> None:
        self.metadata_path = metadata_path
        self.data: Dict[str, Any] = {}
        self.report_proto: Dict[str, Any] = {}
        self._load()

    def _load(self) -> None:
        """Load and parse the metadata JSON file."""
        if not self.metadata_path.exists():
            raise FileNotFoundError(f"Metadata file not found: {self.metadata_path}")
        with open(self.metadata_path, "r", encoding="utf-8") as f:
            self.data = json.load(f)
            self.report_proto = self.data.get("report_proto", {})

    @property
    def report_id(self) -> Optional[str]:
        return self.report_proto.get("ReportID")

    @property
    def product_name(self) -> Optional[str]:
        prod = self.report_proto.get("product", {})
        return prod.get("Name")

    @property
    def build_id(self) -> Optional[str]:
        """Extract the Android Build ID from the product version string (e.g., 0.0.1-15630821)."""
        prod = self.report_proto.get("product", {})
        version_str = prod.get("Version", "")
        if version_str and "-" in version_str:
            return version_str.split("-")[-1]
        return version_str

    @property
    def os_summary(self) -> Optional[str]:
        os_info = self.report_proto.get("os", {})
        name = os_info.get("Name")
        version = os_info.get("Version")
        if name and version:
            return f"{name} ({version})"
        return name or version

    @property
    def build_target(self) -> str:
        """Map OS and CPU architecture to the corresponding Android Build target."""
        os_info = self.report_proto.get("os", {})
        cpu_info = self.report_proto.get("cpu", {})
        os_name = str(os_info.get("Name", "")).lower()
        arch = str(cpu_info.get("Architecture", "")).lower()

        if "linux" in os_name:
            if "arm" in arch or "aarch64" in arch:
                return "emulator_linux_aarch64"
            return "emulator_linux_x64"
        if "mac" in os_name or "darwin" in os_name:
            if "arm" in arch or "aarch64" in arch:
                return "emulator_mac_aarch64"
            return "emulator_mac_x64"
        if "win" in os_name:
            return "emulator_windows_x64"
        return "emulator_linux_x64"

    @property
    def custom_keys(self) -> Dict[str, str]:
        """Extract custom key-value breadcrumbs (e.g., internal-msg)."""
        items = self.report_proto.get("productdata", [])
        result = {}
        for item in items:
            key = item.get("Key")
            val = item.get("Value")
            if key and val:
                result[str(key)] = str(val)
        return result

    @property
    def primary_signature(self) -> Optional[str]:
        return self.report_proto.get("stableSignature")

    @property
    def modules(self) -> List[Tuple[str, str]]:
        """Extract a unique list of (DebugFile, DebugIdentifier) tuples."""
        result = set()
        # Check top-level Module list
        mod_list = self.report_proto.get("Module", [])
        for mod in mod_list:
            df = mod.get("DebugFile")
            di = mod.get("DebugIdentifier")
            if df and di:
                result.add((str(df), str(di)))

        # Check through thread stack frames
        threads = self.report_proto.get("thread", [])
        for t in threads:
            frames = t.get("stack_trace", {}).get("frame", [])
            for f in frames:
                mod = f.get("Module", {})
                df = mod.get("DebugFile")
                di = mod.get("DebugIdentifier")
                if df and di:
                    result.add((str(df), str(di)))
        return list(result)

    def print_summary(self) -> None:
        """Log a concise summary of the decoded crash metadata."""
        logging.info("=== Crash Report Summary ===")
        logging.info("Report ID: %s", self.report_id)
        logging.info("Product: %s (Build ID: %s)", self.product_name, self.build_id)
        logging.info("OS: %s", self.os_summary)
        logging.info("Signature: %s", self.primary_signature)
        for k, v in self.custom_keys.items():
            logging.info("Custom Key [%s]: %s", k, v)
        logging.info("============================")
