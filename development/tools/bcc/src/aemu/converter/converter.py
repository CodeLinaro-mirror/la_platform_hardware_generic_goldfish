# -*- coding: utf-8 -*-
# Copyright 2024 - The Android Open Source Project
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
import logging
from functools import lru_cache
from pathlib import Path

from aemu.converter.apple_converter import AppleClangConverter
from aemu.converter.clang_converter import ClangConverter
from aemu.filters.argument_filter import NormalizeFile
from aemu.process.bazel import Bazel
from aemu.process.runner import run


class Converter:

    def __init__(self, working_dir: str, bazel_exe:str):
        self.bazel = Bazel(working_dir, bazel_exe)

    @lru_cache
    def get_converter(self, compiler: Path):
        out, _ = run([str(compiler), "--version"])
        logging.debug("Compiler version: %s", out)
        if "apple" in out:
            return AppleClangConverter(self.bazel)
        if "clang" in out:
            return ClangConverter(self.bazel)
        if "gcc" in out:
            # TODO(jansene): Gcc?
            return ClangConverter(self.bazel)

        raise NotImplementedError(f"No support for compiler {out}")

    def convert_target(self, target: str):
        normalizer = NormalizeFile(self.bazel)
        description = self.bazel.get_actions(target)

        # Nothing to do..
        if "actions" not in description:
            return []

        entries = []
        for action in description["actions"]:
            # The first parameter is our compiler, we will select the normalizer
            # That is appropriate for our compiler.
            arguments = action["arguments"]
            try:
                cc = normalizer.apply(arguments[0])
                converter = self.get_converter(cc)
                entry = converter.convert(arguments)
                entries.append(entry)
            except ValueError as ve:
                logging.warning("Ignoring entry due to: %s", ve)

        return entries
