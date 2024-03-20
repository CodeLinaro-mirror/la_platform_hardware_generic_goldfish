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
from aemu.converter.compiler_converter import CompilerConverter
from aemu.filters.argument_filter import (
    AndroidClangFilter,
    DropSecondArgumentFilter,
    NormalizeFile,
    DropDeadIncludes,
    SingleArgumentFilter,
)
from aemu.process.bazel import Bazel


class ClangConverter(CompilerConverter):
    def __init__(self, bazel: Bazel):
        super().__init__(bazel)
        self.filters = [
            DropSecondArgumentFilter("-MF|-gcc-toolchain.*"),
            SingleArgumentFilter(
                "-MD|-fdebug-prefix-map.*|-fno-canonical-system-headers.*"
            ),
            AndroidClangFilter(),
            NormalizeFile(self.bazel),
            DropDeadIncludes(bazel),
        ]
