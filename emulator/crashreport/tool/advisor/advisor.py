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
import subprocess
import sys
from typing import List, Optional

from api import CrashApi
from buganizer import (
    BuganizerClient,
    BuganizerError,
    EMULATOR_COMPONENT_ID,
    TAG_DISPATCHED,
)
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
        desc = """Automated Minidump AI Diagnostic Pipeline.

This tool retrieves minidumps and symbols, generates stack traces and looper timelines,
and provisions an AI assistant (jetski/gemini) to investigate the root cause.

Common Workflows:
  1. Interactive AI Investigation (Default):
     $ bazel run @goldfish//emulator/crashreport/tool/advisor -- 6998451fea502b78 --token "<TOKEN>"
     Then run the generated script in your terminal to open the AI REPL:
       /tmp/crashadvisor_$USER/6998451fea502b78/investigation_cmd.sh

  2. Automated Batch Mode (--auto-run):
     $ bazel run @goldfish//emulator/crashreport/tool/advisor -- 6998451fea502b78 --token "<TOKEN>" --auto-run
     Executes the AI investigation in the background and saves the Root Cause Analysis
     directly to rca_summary.md without waiting for user prompts.

  3. Closed-Loop Buganizer Mode (--auto-run --enable-buganizer):
     $ bazel run @goldfish//emulator/crashreport/tool/advisor -- 6998451fea502b78 --token "<TOKEN>" --auto-run --enable-buganizer
     Automatically searches for or creates the relevant Buganizer issue in Component 29601,
     attaches the RCA summary, and dispatches an autonomous engineer to draft a fix.

Prerequisite: OAuth2 Token
  CrashAdvisor requires an OAuth2 token to fetch symbols from Android Build (go/ab)
  and manage Buganizer tickets. Generate one on GLinux/Cloudtop via:
    oauth2l reset && oauth2l fetch --sso $USER@google.com https://www.googleapis.com/auth/buganizer https://www.googleapis.com/auth/androidbuild.internal"""
        parser = argparse.ArgumentParser(
            description=desc,
            formatter_class=argparse.RawDescriptionHelpFormatter,
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
            "-b",
            "--branch",
            help="Target Android Build branch (e.g., emu-main-dev) to determine correct go/ab targets",
        )
        parser.add_argument(
            "--build-id",
            help="Explicit Android Build ID (e.g., 15630821) to override the version parsed from metadata (typically only needed for older emulator builds)",
        )
        parser.add_argument(
            "-v",
            "--verbose",
            action="store_true",
            help="Enable verbose debug logging",
        )
        parser.add_argument(
            "--auto-run",
            action="store_true",
            help="Automatically execute the AI investigation script immediately in non-interactive batch mode",
        )
        parser.add_argument(
            "--enable-buganizer",
            action="store_true",
            help="Explicitly enable Buganizer issue creation, search, and comment updates (disabled by default)",
        )
        parser.add_argument(
            "--bug-id",
            type=int,
            help="Explicit Buganizer issue ID (e.g., 12345678) to attach findings to",
        )
        parser.add_argument(
            "--timeout",
            default="30m",
            help="Execution timeout for the underlying AI investigation CLI (default: 30m)",
        )
        parser.add_argument(
            "--work-dir",
            help="Custom working directory sandbox path for crash artifacts",
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
            self.context = CrashReportContext(
                self.args.crash_id,
                aosp_root=self.args.aosp_root,
                branch=self.args.branch,
                build_id=self.args.build_id,
                work_dir=self.args.work_dir,
            )
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
            metadata = CrashMetadata(
                self.context.metadata_path,
                branch=self.context.branch,
                build_id=self.context.build_id,
            )
            metadata.print_summary()

            # Step 3: Fetch Symbols
            symbols_dir = self.context.work_dir / "symbols"
            fetcher = SymbolFetcher(self.api)
            fetcher.fetch_symbols(metadata, symbols_dir, self.args.token)

            # Step 4: Generate Complete Local Dump
            dumper = CrashReportDumper()
            dump_path = dumper.generate_dump(self.context, symbols_dir)

            # Step 5: Prepare Jetski AI Investigation Script
            analyzer = CrashReportAnalyzer()
            investigation_script = analyzer.generate_explanation(
                self.context,
                dump_path,
                auto_run=self.args.auto_run,
                timeout=self.args.timeout,
                metadata=metadata,
            )
            if self.args.auto_run:
                logging.info(
                    "Launching AI investigation automatically: %s", investigation_script
                )
                subprocess.run([str(investigation_script)], check=True)

                # Step 6: Closed-Loop Buganizer Integration & Agent Handoff (Disabled by default)
                if self.args.enable_buganizer:
                    self._process_closed_loop(metadata)
                else:
                    logging.info(
                        "Buganizer integration is disabled by default. Pass --enable-buganizer to enable closed-loop issue updates."
                    )
            else:
                logging.info(
                    "CrashAdvisor pipeline complete. Launch investigation script: %s",
                    investigation_script,
                )
        except Exception as e:
            logging.error("%s", e, exc_info=self.args.verbose)
            sys.exit(1)

    def _process_closed_loop(self, metadata: CrashMetadata) -> None:
        """Execute Buganizer lifecycle updates and autonomous agent handoff."""
        logging.info("=== Executing Closed-Loop Buganizer & Agent Handoff ===")
        rca_summary_path = self.context.work_dir / "rca_summary.md"
        if not rca_summary_path.exists():
            md_files = list(self.context.work_dir.glob("*.md"))
            if md_files:
                rca_summary_path = md_files[0]
            else:
                logging.warning(
                    "No markdown RCA summary found in %s. Skipping Buganizer update.",
                    self.context.work_dir,
                )
                return

        rca_content = rca_summary_path.read_text(encoding="utf-8")
        stable_sig = getattr(
            metadata, "stable_signature", f"Crash-{self.context.crash_id}"
        )

        try:
            buganizer = BuganizerClient(
                token=self.args.token, verbose=self.args.verbose
            )
            bug_id = self.args.bug_id

            if not bug_id:
                logging.info(
                    "Searching Buganizer for existing issue matching stableSignature: %s",
                    stable_sig,
                )
                existing_issue = buganizer.search_issue_by_signature(stable_sig)
                if existing_issue:
                    bug_id = int(
                        existing_issue.get("id", existing_issue.get("issueId", 0))
                    )
                    status = existing_issue.get(
                        "status",
                        existing_issue.get("issueState", {}).get("status", "NEW"),
                    )
                    logging.info(
                        "Found existing issue b/%d (status: %s)", bug_id, status
                    )
                    if status in ("FIXED", "VERIFIED", "OBSOLETE"):
                        build_id_str = self.metadata.build_id if self.metadata else "unknown"
                        logging.info(
                            "Issue b/%d is %s. Crash originated on Build ID %s (older repository version context).",
                            bug_id,
                            status,
                            build_id_str,
                        )
                        buganizer.update_issue_comment(
                            bug_id,
                            f"Crash report captured on Build ID {build_id_str} (older repository version):\n{rca_content}",
                        )
                    else:
                        logging.info("Issue is open. Adding RCA comment.")
                        buganizer.update_issue_comment(bug_id, rca_content)
                else:
                    logging.info(
                        "No existing issue found. Creating new issue in Component %d",
                        EMULATOR_COMPONENT_ID,
                    )
                    bug_id = buganizer.create_issue(stable_sig, rca_content)
                    logging.info("Created new issue b/%d", bug_id)

            # Step 7: Parse YAML actionability block for autonomous agent handoff
            self._execute_agent_handoff(buganizer, bug_id, rca_content)

        except BuganizerError as e:
            logging.error(
                "Buganizer integration failed: %s", e, exc_info=self.args.verbose
            )

    def _execute_agent_handoff(
        self, buganizer: BuganizerClient, bug_id: int, rca_content: str
    ) -> None:
        """Parse YAML actionability and dispatch emu_main_next_engineer via agentapi."""
        if "actionability:" not in rca_content:
            logging.info(
                "No structured actionability block found in RCA summary. Halting for human review."
            )
            return

        lines = (
            rca_content.split("actionability:")[1].split("```")[0].strip().split("\n")
        )
        action_data = {}
        for line in lines:
            if ":" in line:
                key, val = line.split(":", 1)
                action_data[key.strip()] = (
                    val.strip().strip('"').strip("'").split("#")[0].strip()
                )

        if action_data.get("fixable", "false").lower() != "true":
            logging.info(
                "Actionability indicates root cause is not autonomously fixable. Halting for human review."
            )
            return

        logging.info(
            "Structured actionability asserts fixable: true. Verifying Buganizer tags to prevent runaway loops."
        )
        try:
            buganizer.update_issue_comment(
                bug_id,
                "Dispatching emu_main_next_engineer for autonomous fix.",
                tags=[TAG_DISPATCHED],
            )
        except BuganizerError as e:
            logging.warning("Failed to update Buganizer tags: %s", e)

        target_file = action_data.get("target_file", "unknown")
        target_function = action_data.get("target_function", "unknown")
        remediation = action_data.get("remediation_summary", "Refer to rca_summary.md")

        build_id = self.metadata.build_id if self.metadata else "unknown"
        version_str = (
            f"{self.metadata.product_name} ({self.metadata.build_id})"
            if self.metadata and self.metadata.product_name
            else build_id
        )

        existing_comments = (
            "\n".join(buganizer.get_issue_comments(bug_id)[-5:]) if bug_id else "None"
        )

        engineer_prompt = f"""Implement the remediation plan detailed in rca_summary.md for Bug: {bug_id}.
Target File: {target_file}
Target Function: {target_function}
Remediation Summary: {remediation}

⚠️ CRASH BUILD & REPOSITORY VERSION CONTEXT:
• Crash Build ID: {build_id} (Version: {version_str})
• Warning: This crash was captured from a build compiled against an older version of the repository than the current workspace (HEAD). The bug may already have been fixed in the current codebase!

BEFORE WRITING CODE OR MAKING MODIFICATIONS:
1. Inspect git log for {target_file} and verify if {target_function} or this crash signature has already been modified or fixed in newer commits.
2. Inspect the current source code at {target_file} to verify whether the faulting condition (e.g., null pointer dereference, bounds overflow) still exists at HEAD.
3. If the bug is ALREADY FIXED in the current workspace:
   - Verify existing test coverage or write a unit test to confirm the fix.
   - Document the existing fixing commit/CL in your final response.
   - Do NOT introduce redundant, duplicate, or conflicting code changes.
4. If and only if the bug is confirmed to still exist in the current workspace, follow the TDD loop (Red/Green/Refactor) coordinating with test_enforcer.

=== EXISTING BUGANIZER COMMENTS ===
{existing_comments}
===================================

Follow the TDD loop (Red/Green/Refactor) coordinating with test_enforcer.
Upon successful verification, hand off to reviewer.md for audit and committer.md to execute repo upload.
"""

        logging.info("Spawning emu_main_next_engineer via agentapi...")
        try:
            subprocess.run(
                [
                    "agentapi",
                    "new-conversation",
                    "--model=pro",
                    "--agent=.gemini/agents/emu_main_next_engineer.md",
                    engineer_prompt,
                ],
                check=True,
            )
            logging.info("Autonomous engineer successfully dispatched.")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            logging.warning(
                "agentapi invocation failed or not found in environment: %s", e
            )


def main() -> None:
    app = CrashAdvisorApp()
    app.run()


if __name__ == "__main__":
    main()
