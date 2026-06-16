#!/usr/bin/env python3
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

"""CrashAdvisor (advisor): Automated Minidump AI Diagnostic Pipeline.

Provides a modular, testable object-oriented architecture for fetching and analyzing
crash reports from crash.corp.google.com.
"""

import argparse
import logging
import sys
from typing import List, Optional

from api import CrashApi
from client import GossoClient
from context import CrashReportContext
from dump import CrashReportDumper
from metadata import CrashMetadata
from rca import CrashReportAnalyzer
from symbols import SymbolFetcher


class CrashAdvisorApp:
    """Orchestrates the CLI application lifecycle and execution flow."""

    def __init__(self, args_list: Optional[List[str]] = None) -> None:
        self.args = self._parse_args(args_list)
        self._configure_logging()
        self.context: Optional[CrashReportContext] = None
        self.gosso: Optional[GossoClient] = None
        self.api: Optional[CrashApi] = None

    def _parse_args(self, args_list: Optional[List[str]]) -> argparse.Namespace:
        parser = argparse.ArgumentParser(
            description="CrashAdvisor (advisor): Automated Minidump AI Diagnostic Pipeline."
        )
        parser.add_argument(
            "crash_id",
            help="The crash ID or go/crash URL to analyze (e.g., 6998451fea502b78 or http://go/crash/6998451fea502b78)",
        )
        parser.add_argument(
            "--token",
            help="OAuth2 token generated on GLinux for Android Build (go/ab) single zip downloading",
        )
        parser.add_argument(
            "-r",
            "--aosp-root",
            help="Explicit path to an alternate AOSP workspace root (e.g., for qemu2 branch analysis)",
        )
        parser.add_argument(
            "-v",
            "--verbose",
            action="store_true",
            help="Enable verbose debug logging",
        )
        return parser.parse_args(args_list)

    def _configure_logging(self) -> None:
        log_level = logging.DEBUG if self.args.verbose else logging.INFO
        logging.basicConfig(
            level=log_level,
            format="%(asctime)s [%(levelname)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
        )

    def run(self) -> None:
        """Execute the ingestion and metadata parsing pipeline."""
        logging.debug(
            "Starting CrashAdvisor pipeline for input: %s", self.args.crash_id
        )
        try:
            self.context = CrashReportContext(self.args.crash_id, aosp_root=self.args.aosp_root)
            self.context.prepare_sandbox()
            self.gosso = GossoClient()
            self.api = CrashApi(self.gosso, verbose=self.args.verbose)

            # Step 1: Download Assets
            self.api.download_metadata(
                self.context.crash_id, self.context.metadata_path
            )
            self.api.download_minidump(
                self.context.crash_id, self.context.minidump_path
            )

            # Step 2: Parse & Verify Metadata
            metadata = CrashMetadata(self.context.metadata_path)
            metadata.print_summary()

            # Step 3: Fetch Symbols
            symbols_dir = self.context.work_dir / "symbols"
            fetcher = SymbolFetcher(self.api)
            fetcher.fetch_symbols(metadata, symbols_dir, self.args.token)

            # Step 4: Generate Complete Local Dump
            dumper = CrashReportDumper()
            dump_path = dumper.generate_dump(self.context, symbols_dir)

            # Step 5: Prepare Interactive Jetski AI Investigation Script
            analyzer = CrashReportAnalyzer()
            investigation_script = analyzer.generate_explanation(
                self.context, dump_path
            )
            logging.info(
                "CrashAdvisor pipeline complete. Launch investigation script: %s",
                investigation_script,
            )
        except Exception as e:
            logging.error("%s", e, exc_info=self.args.verbose)
            sys.exit(1)


def main() -> None:
    app = CrashAdvisorApp()
    app.run()


if __name__ == "__main__":
    main()
