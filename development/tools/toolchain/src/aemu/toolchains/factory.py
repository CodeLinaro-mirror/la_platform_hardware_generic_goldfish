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
"""Factory for creating toolchain generators."""
from pathlib import Path

from aemu.toolchains.darwin_generator import (
    DarwinToDarwinGenerator,
    DarwinToDarwinX64Generator,
)
from aemu.toolchains.linux_arm_generator import LinuxToLinuxAarch64Generator
from aemu.toolchains.linux_generator import LinuxToLinuxGenerator
from aemu.toolchains.toolchain_generator import ToolchainGenerator
from aemu.toolchains.windows_generator import WindowsToWindowsGenerator

TARGET_ALIAS = {
    "emulator-windows_x64": "windows-x64",
    "windows": "windows-x64",
    "windows-msvc-x86_64": "windows-x64",
    "linux": "linux-x64",
    "trusty": "linux-x64",
    "emulator-linux_x64": "linux-x64",
    "linux-x86_64": "linux-x64",
    "linux-aarch64": "linux-aarch64",
    "darwin": "mac-aarch64",
    "darwin-x86_64": "mac-x64",
    "darwin-aarch64": "mac-aarch64",
    "emulator-mac_aarch64": "mac-aarch64",
}


def get_toolchain_generator(
    target: str, toolchain_dir: Path, prefix: str, aosp: Path
) -> ToolchainGenerator:
    """Factory method for ToolchainGenerator objects.

    This function returns a ToolchainGenerator object for the given target.

    Args:
        target: The target platform.
        toolchain_dir: The directory where the toolchain will be installed.
        prefix: The prefix for the toolchain binaries.
        aosp: The path to the AOSP source tree.

    Returns:
        A ToolchainGenerator object.

    Raises:
        ValueError: If the target is not supported.
    """
    # TODO: __host__To__target__ Toolchain generator.
    # Note that if you wish to add cross compilation support
    # You will have to add this support to the bazel toolchains
    # as well, as we depend on bazel for pkg-config lib + include
    # generation.
    generator_map = {
        "windows-x64": WindowsToWindowsGenerator,
        "linux-x64": LinuxToLinuxGenerator,
        "linux-aarch64": LinuxToLinuxAarch64Generator,
        "mac-aarch64": DarwinToDarwinGenerator,
        "mac-x64": DarwinToDarwinX64Generator,
    }

    if target not in TARGET_ALIAS:
        raise ValueError(f"No toolchain support for target: {target}")

    toolchain_klazz = generator_map[TARGET_ALIAS[target]]
    # Initialize the toolchain generator with the specified destination and an empty suffix.
    # This generator will be used to manage toolchain-related configurations.
    return toolchain_klazz(Path(aosp), Path(toolchain_dir), prefix)
