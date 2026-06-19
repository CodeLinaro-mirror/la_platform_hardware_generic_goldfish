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

"""Generates the interactive Jetski AI investigation command for minidump root cause analysis."""

import logging
import os
import shlex
from pathlib import Path

from context import CrashReportContext


class CrashReportAnalyzer:
    """Generates the interactive Jetski AI investigation command for minidump root cause analysis."""

    def _find_aosp_root(self, start_path: Path) -> str:
        """Walk up the directory tree to robustly locate the AOSP workspace root."""
        if "BUILD_WORKSPACE_DIRECTORY" in os.environ:
            return os.environ["BUILD_WORKSPACE_DIRECTORY"]
        current = start_path.resolve()
        for parent in [current] + list(current.parents):
            if (parent / "hardware" / "generic" / "goldfish").exists():
                return str(parent)
            if (
                (parent / "WORKSPACE").exists()
                or (parent / "WORKSPACE.bazel").exists()
                or (parent / "MODULE.bazel").exists()
            ):
                return str(parent)
        return str(start_path.parents[4])

    def generate_explanation(
        self, context: CrashReportContext, dump_path: Path, auto_run: bool = False, timeout: str = "30m"
    ) -> Path:
        """Construct the Jetski CLI command and save it to investigation_cmd.sh."""
        investigation_script = context.work_dir / "investigation_cmd.sh"

        if not dump_path.exists() or dump_path.stat().st_size == 0:
            raise FileNotFoundError(
                f"Crash dump file {dump_path} is missing or empty. Cannot prepare investigation."
            )

        # Dynamically locate the AOSP workspace root and co-located agent definition
        aosp_root = context.aosp_root or self._find_aosp_root(Path(__file__))
        agent_md = Path(__file__).resolve().parent / "crash_advisor.md"

        logging.info(
            "Preparing interactive Jetski prompt referencing local dump: %s", dump_path
        )

        prompt = f"""You are an expert Android Emulator Host & Concurrency System Debugger.
Your mission is to perform a rigorous Root Cause Analysis (RCA) on the attached emulator minidump extraction: `{dump_path.name}`.

=== ANALYSIS INSTRUCTIONS ===
1. Use your read tools to examine the crashing thread stack frames and register states in `{dump_path.name}`. Identify the immediate failure instruction (e.g., null dereference, assertion failure, segmentation fault).
2. Evaluate the Mermaid sequence diagram and decoded looper breadcrumb timeline in `{dump_path.name}`. Determine which thread initiated the fatal asynchronous flow or where lock ordering inverted.
3. Ground all statements strictly in the visible stack frames and breadcrumb timelines. Do not make unverified claims.
4. Use your search and read tools to inspect the active AOSP codebase.
5. Upon completion of your investigation, create a comprehensive, highly structured markdown document that is shown to the user and written to disk.
"""

        # Construct the command arguments using --prompt / --prompt-interactive and --add-dir
        prompt_flag = "--prompt" if auto_run else "--prompt-interactive"
        cmd_args = [
            "--model=pro",
            f"--agent={agent_md}",
            f"--print-timeout={timeout}",
            f"--add-dir={aosp_root}",
            f"--add-dir={dump_path.parent}",
            prompt_flag,
            prompt,
        ]

        cmd_str = " ".join(shlex.quote(arg) for arg in cmd_args)

        with open(investigation_script, "w", encoding="utf-8") as f:
            f.write("#!/usr/bin/env bash\n")
            f.write(
                f"# Interactive Jetski AI investigation for Crash ID {context.crash_id}\n\n"
            )
            f.write("if command -v jetski >/dev/null 2>&1; then\n")
            f.write('    CLI_BIN="jetski"\n')
            f.write("elif [ -x /google/bin/releases/jetski-devs/tools/cli ]; then\n")
            f.write('    CLI_BIN="/google/bin/releases/jetski-devs/tools/cli"\n')
            f.write("elif [ -x /google/bin/releases/gemini-cli/tools/gemini ]; then\n")
            f.write('    CLI_BIN="/google/bin/releases/gemini-cli/tools/gemini"\n')
            f.write("elif command -v gemini >/dev/null 2>&1; then\n")
            f.write('    CLI_BIN="gemini"\n')
            f.write("else\n")
            f.write(
                '    echo "Error: No jetski or gemini CLI installation found." >&2\n'
            )
            f.write("    exit 1\n")
            f.write("fi\n\n")
            f.write(f'exec "$CLI_BIN" {cmd_str}\n')
        investigation_script.chmod(0o755)

        logging.info("=== CrashAdvisor Environment Ready ===")
        logging.info(
            "All crash assets, minidumps, and symbols have been successfully extracted to:"
        )
        logging.info("  %s", context.work_dir)
        logging.info("")
        if auto_run:
            logging.info(
                "Launching Jetski automatically in non-interactive batch mode..."
            )
        else:
            logging.info(
                "To launch Jetski interactively and begin the AI investigation, run:"
            )
            logging.info("  %s", investigation_script)
        logging.info("======================================")

        return investigation_script
