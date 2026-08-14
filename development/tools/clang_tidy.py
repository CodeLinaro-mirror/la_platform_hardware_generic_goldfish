#!/usr/bin/env python3
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
"""Preupload and local validation hook for clang-tidy using Bazel.

This script identifies modified C/C++ files and extracts modified line ranges
from git diffs to run targeted, fast-fail clang-tidy checks via Bazel aspects.
"""

import argparse
import json
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Dict, List, Optional


def _find_repo_root() -> Path:
    if "REPO_ROOT" in os.environ:
        return Path(os.environ["REPO_ROOT"]).resolve()
    if "BUILD_WORKSPACE_DIRECTORY" in os.environ:
        return Path(os.environ["BUILD_WORKSPACE_DIRECTORY"]).resolve()
    root = Path(__file__).resolve().parent
    while root != root.parent:
        if (root / ".repo").is_dir() or (
            root / "tools" / "buildSrc" / "servers"
        ).is_dir():
            return root.resolve()
        root = root.parent
    return Path(__file__).resolve().parents[4]


_REPO_ROOT = _find_repo_root()
_SERVERS_DIR = _REPO_ROOT / "tools" / "buildSrc" / "servers"
if str(_SERVERS_DIR) not in sys.path:
    sys.path.insert(0, str(_SERVERS_DIR))

import bazel
import build_environment

CPP_EXTENSIONS = (
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".c++",
    ".C",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".h++",
    ".H",
)

_FILE_PATTERN = re.compile(r"^diff --git a/(.*?) b/(.*?)$")
_CHUNK_PATTERN = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def is_cpp_file(path: Path) -> bool:
    """Checks if the given file path corresponds to a C/C++ source or header file."""
    return any(path.name.endswith(ext) for ext in CPP_EXTENSIONS)


def _extract_diff_lines(diff_text: str) -> Dict[str, List[List[int]]]:
    """Parses unified git diff output into a mapping of filename -> line ranges."""
    file_lines: Dict[str, List[List[int]]] = {}
    current_file = None

    for line in diff_text.splitlines():
        file_match = _FILE_PATTERN.match(line)
        if file_match:
            current_file = file_match.group(2)
            if current_file not in file_lines:
                file_lines[current_file] = []
            continue

        chunk_match = _CHUNK_PATTERN.match(line)
        if chunk_match and current_file:
            start_line = int(chunk_match.group(1))
            length = chunk_match.group(2)
            length = int(length) if length is not None else 1
            if length > 0:
                end_line = start_line + length - 1
                file_lines[current_file].append([start_line, end_line])

    return file_lines


def _get_git_diff(
    goldfish_root: Path, commit: Optional[str] = None
) -> Dict[str, List[List[int]]]:
    """Retrieves diff hunks for the specified commit or uncommitted working changes."""
    diff_outputs = []

    if commit:
        cmd = ["git", "-C", str(goldfish_root), "show", commit]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            diff_outputs.append(res.stdout)
        else:
            logging.warning("Failed to run git show %s: %s", commit, res.stderr)
    else:
        # Check staged + unstaged changes against HEAD
        cmd = ["git", "-C", str(goldfish_root), "diff", "HEAD"]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0 and res.stdout:
            diff_outputs.append(res.stdout)
        else:
            # Fallback to unstaged diff if repo has no commits yet
            cmd = ["git", "-C", str(goldfish_root), "diff"]
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode == 0 and res.stdout:
                diff_outputs.append(res.stdout)

    all_file_lines: Dict[str, List[List[int]]] = {}
    for diff_text in diff_outputs:
        for fname, lines in _extract_diff_lines(diff_text).items():
            all_file_lines.setdefault(fname, []).extend(lines)

    return all_file_lines


def _extract_and_log_clang_tidy_failures(bzl_tidy: bazel.BazelCmd) -> None:
    """Extracts and logs clang-tidy violations from bazel test logs."""
    try:
        if "bazel-testlogs" not in bzl_tidy.info:
            return
        testlogs_dir = Path(bzl_tidy.info["bazel-testlogs"])
        if not testlogs_dir.is_dir():
            return

        for log_val in testlogs_dir.rglob("test.log"):
            content = log_val.read_text(encoding="utf-8", errors="replace")
            if "Clang-tidy found issues" in content or "clang-tidy crashed" in content:
                try:
                    rel_path = log_val.relative_to(testlogs_dir)
                except ValueError:
                    rel_path = log_val
                logging.error(
                    "\n"
                    + "=" * 80
                    + f"\n[LINT FAILURE] Report from: {rel_path}\n"
                    + "=" * 80
                    + f"\n{content}\n"
                    + "=" * 80
                )
    except (OSError, KeyError, build_environment.CommandFailedException) as e:
        logging.warning("Failed to extract clang-tidy logs: %s", e)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="""
        Validates C/C++ files against clang-tidy rules using Bazel.

        This script checks modified C/C++ files for clang-tidy errors.
        It uses unified diff information to restrict analysis to modified
        lines, ensuring fast and targeted presubmit checks. Files that are
        not recognized as C/C++ files are ignored.
        """,
        epilog="""Examples:
        Check a list of files:
            ./clang_tidy.py file1.cc file2.h

        Check files for a specific commit:
            ./clang_tidy.py --commit HEAD~1

        The script exits with a non-zero code if tidy errors are found.
        """,
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "--commit",
        default=os.environ.get("PREUPLOAD_COMMIT"),
        help="Commit hash to extract diff line filters from (defaults to $PREUPLOAD_COMMIT).",
    )
    parser.add_argument(
        "files",
        nargs="*",
        help="List of files to check (defaults to $PREUPLOAD_FILES if set).",
    )

    args = parser.parse_args()

    input_files = args.files
    if not input_files and "PREUPLOAD_FILES" in os.environ:
        input_files = os.environ["PREUPLOAD_FILES"].split()

    goldfish_root = (_REPO_ROOT / "hardware" / "generic" / "goldfish").resolve()

    # If no files specified, inspect git diff for modified files
    diff_file_lines = _get_git_diff(goldfish_root, args.commit)

    if input_files:
        cpp_files = [Path(x) for x in input_files if is_cpp_file(Path(x))]
    else:
        cpp_files = [
            goldfish_root / fname
            for fname in diff_file_lines.keys()
            if is_cpp_file(Path(fname))
        ]

    if not cpp_files:
        print("No C/C++ files found, ignoring")
        return 0

    filters = []
    check_file_flags = []

    for f in cpp_files:
        # Determine relative path within goldfish repository or workspace
        resolved = f.resolve() if f.is_absolute() else (goldfish_root / f).resolve()
        if resolved.is_relative_to(goldfish_root):
            rel_name = resolved.relative_to(goldfish_root).as_posix()
        elif resolved.is_relative_to(_REPO_ROOT):
            rel_name = resolved.relative_to(_REPO_ROOT).as_posix()
        else:
            rel_name = f.as_posix()

        check_file_flags.append(
            f"--@goldfish_build//:clang_tidy_check_files={rel_name}"
        )

        # Look up diff line ranges for this file
        matched_lines = None
        for diff_fname, lines in diff_file_lines.items():
            if (
                diff_fname == rel_name
                or diff_fname.endswith(rel_name)
                or rel_name.endswith(diff_fname)
                or Path(diff_fname).name == Path(rel_name).name
            ):
                matched_lines = lines
                break

        if matched_lines:
            filters.append({"name": rel_name, "lines": matched_lines})
        elif not diff_file_lines:
            # If no git diff was available, do not restrict lines for this file
            pass

    tidy_build_options = [
        "--@goldfish_build//:clang_tidy_enabled=true",
    ] + check_file_flags

    if filters:
        line_filter_json = json.dumps(filters)
        tidy_build_options.append(
            f"--@goldfish_build//:clang_tidy_line_filter={line_filter_json}"
        )

    with build_environment.create_bazel_environment(args) as env:
        bzl_tidy = bazel.BazelCmd(env).with_build_flags(tidy_build_options)
        try:
            res = bzl_tidy.test(
                ["@goldfish//..."],
                invocation_flags=[
                    "--build_tests_only",
                    "--test_tag_filters=tidy-test",
                    "--test_output=errors",
                    "--modify_execution_info=ClangTidy.*=+no-remote",
                ],
                allow_analysis_cache_discard=True,
                timeout=600,
            )
            if res.returncode != 0:
                _extract_and_log_clang_tidy_failures(bzl_tidy)
                return res.returncode
            return 0
        except (
            build_environment.CommandFailedException,
            subprocess.TimeoutExpired,
        ) as e:
            _extract_and_log_clang_tidy_failures(bzl_tidy)
            return 1


if __name__ == "__main__":
    sys.exit(main())
