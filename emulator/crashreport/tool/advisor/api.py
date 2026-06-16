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

"""Interacts with crash.corp.google.com endpoints to fetch report assets."""

import json
import logging
from pathlib import Path
from typing import Any, Dict, Optional

from client import GossoClient


class CrashApi:
    """Interacts with crash.corp.google.com endpoints to fetch report assets."""

    BASE_URL = "https://crash.corp.google.com"

    def __init__(self, gosso_client: GossoClient, verbose: bool = False) -> None:
        self.client = gosso_client
        self.verbose = verbose

    def download_metadata(self, crash_id: str, output_path: Path) -> None:
        """Fetch the full crash report JSON metadata (report_proto) using response_type=json."""
        if output_path.exists() and output_path.stat().st_size > 0:
            logging.info("Metadata already cached locally at: %s", output_path)
            return
        url = f"{self.BASE_URL}/get_report?id={crash_id}&response_type=json"
        logging.info("Fetching report metadata for crash ID %s...", crash_id)
        self.client.fetch_url_to_file(url, output_path)

        # Pretty-print the downloaded JSON in-place for exceptional developer ergonomics if verbose is enabled
        if self.verbose and output_path.exists() and output_path.stat().st_size > 0:
            try:
                with open(output_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                with open(output_path, "w", encoding="utf-8") as f:
                    json.dump(data, f, indent=2)
                logging.debug("Successfully pretty-printed metadata.json with indent=2")
            except Exception as e:
                logging.warning("Failed to pretty-print metadata.json: %s", e)

    def download_minidump(self, crash_id: str, output_path: Path) -> None:
        """Fetch the raw binary minidump (.dmp) file."""
        if output_path.exists() and output_path.stat().st_size > 0:
            logging.info("Minidump already cached locally at: %s", output_path)
            return
        url = f"{self.BASE_URL}/fetch_file?reportid={crash_id}&filename=upload_file_minidump"
        logging.info("Fetching raw minidump for crash ID %s...", crash_id)
        self.client.fetch_url_to_file(url, output_path)
