# -*- coding: utf-8 -*-
# Copyright 2023 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import json
import logging
import platform
import re
import shlex
import shutil
import subprocess
from functools import lru_cache
from typing import Dict, Set

from aemu.process.runner import check_output, run


class Bazel:
    def __init__(self, cwd, aosp):
        self.cwd = cwd
        self.exe = shutil.which("bazel")
        if not self.exe:
            self.exe = shutil.which(
                "bazel", path=aosp / "prebuilts" / "bazel" / f"{self.host()}-x86_64"
            )

        if not self.exe:
            raise FileNotFoundError("No bazel installation found!")

        self.info = self._load_bazel_info()
        self.version = self._get_bazel_version()
        logging.debug("Using bazel config: %s", self.info)

    def host(self) -> str:
        return platform.system().lower()

    def build_target(self, bazel_target: str) -> str:
        """Builds the specified Bazel target.

        Run the Bazel build command for the specified target using the
        stored Bazel executable.

        Args:
            bazel_target (str): The Bazel target to build.

        Returns:
            List of targets that were build.
        """
        label = bazel_target[bazel_target.index(":") + 1 :]
        bazel_explain_file = (self.log_dir / f"explain-{label}.txt").absolute()
        output = run(
            [
                self.exe,
                "build",
                f"--explain={bazel_explain_file}",
                "--verbose_explanations",
                bazel_target,
            ],
            cwd=self.cwd,
        )
        return [x for x in output if x.startswith("bazel")]

    def _get_bazel_version(self):
        match = re.search(r"(\d+)\.(\d+)\.(\d+)$", self.info["release"])
        if not match:
            logging.warning("Unable to get Bazel version, returning 0")
            return (0, 0, 0)

        return tuple(int(match.group(i)) for i in range(1, 4))

    @lru_cache(maxsize=None)
    def get_actions(self, target: str):
        # See: https://docs.bazel.build/versions/master/aquery.html

        aquery = [
            self.exe,
            "aquery",
            f"mnemonic('(Objc|Cpp)Compile',deps({target}))",
            "--output=jsonproto",
            "--include_artifacts=false",
            "--ui_event_filters=-info",
            "--noshow_progress",
            "--features=-compiler_param_file",
        ]

        try:
            result, _ = run(aquery, cwd=self.cwd)
            return json.loads(result)
        except subprocess.CalledProcessError as cpe:
            logging.error("Failed to run %s do to: %s", aquery, cpe)
            raise cpe

    def _load_bazel_info(self) -> Dict[str, str]:
        """Retrieve the bazel configuration."""

        # Bazel info gives:
        # key: value
        #
        # for example:
        #
        # command_log: /private/var/tmp/_bazel_me/66e1d3546ce0030819ddb695de13f5d3/command.log
        # committed-heap-size: 209MB
        # execution_root: /private/var/tmp/_bazel_me/66e1d3546ce0030819ddb695de13f5d3/execroot/_main
        # gc-count: 121
        info = check_output(cmd=[self.exe, "info"], cwd=self.cwd).splitlines()
        return dict(line.strip().split(": ") for line in info)

    def closure(self, target) -> Set[str]:
        query = [self.exe, "query", f"kind('.*_library', deps({target}))"]
        return set(check_output(query, cwd=self.cwd).splitlines())
