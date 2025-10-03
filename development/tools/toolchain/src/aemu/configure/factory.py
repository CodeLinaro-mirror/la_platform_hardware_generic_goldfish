# -*- coding: utf-8 -*-
# Copyright 2025 - The Android Open Source Project
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
"""Factory for creating QemuBuilder objects."""
from pathlib import Path
from typing import List

from aemu.configure.base_builder import QemuBuilder
from aemu.configure.darwin_builder import DarwinBuilder
from aemu.configure.linux_builder import LinuxBuilder
from aemu.configure.trusty_builder import TrustyBuilder
from aemu.configure.windows_builder import WindowsBuilder
from aemu.toolchains.factory import TARGET_ALIAS, get_toolchain_generator


def get_builder(
    target: str,
    dest: Path,
    toolchain_dir: Path,
    prefix: str,
    aosp: Path,
    ccache: Path,
    bazel_startup_options: List[str],
    bazel_build_options: List[str],
) -> QemuBuilder:
    """Factory method for QemuBuilder objects.

    This function returns a QemuBuilder object for the given target.

    Args:
        target: The target platform.
        dest: The destination directory for the build.
        toolchain_dir: The directory where the toolchain is installed.
        prefix: The prefix for the toolchain binaries.
        aosp: The path to the AOSP source tree.
        ccache: The path to the ccache binary.
        bazel_startup_options: A list of startup options for Bazel.
        bazel_build_options: A list of build options for Bazel.

    Returns:
        A QemuBuilder object.
    """
    builder_map = {
        "windows-x64": WindowsBuilder,
        "linux-x64": LinuxBuilder,
        "linux-aarch64": LinuxBuilder,
        "mac-aarch64": DarwinBuilder,
        "mac-x64": DarwinBuilder,
        "trusty": TrustyBuilder,
    }

    # Get the class that is capable of configuring the toolchain
    # from the current host targeting our target.
    toolchain = get_toolchain_generator(target, toolchain_dir, prefix, aosp)

    if target not in builder_map:
        target = TARGET_ALIAS[target]

    return builder_map[target](
        Path(aosp),
        Path(dest),
        Path(toolchain_dir),
        ccache,
        toolchain,
        bazel_startup_options,
        bazel_build_options,
    )
