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
import re
from pathlib import Path

from aemu.converter.clang_converter import ClangConverter
from aemu.filters.argument_filter import SingleArgumentFilter
from aemu.process.bazel import Bazel
from aemu.process.runner import check_output


class ApplySysrootFilter(SingleArgumentFilter):
    def __init__(self, sysroot):
        super().__init__("--sysroot.*", self.fixup)
        self.sysroot = str(sysroot)

    def fixup(self, root):
        return ["-isysroot", self.sysroot]


def compare_versions(v1: str, v2: str) -> int:
    """Compares two version strings.

    Args:
        v1 (str): The first version string.
        v2 (str): The second version string.

    Returns:
        int: -1 if v1 is older than v2, 0 if v1 is equal to v2, and 1 if v1 is newer than v2.
    """

    # Split the version strings into lists of numbers
    v1_list = [int(x) for x in v1.split(".")]
    v2_list = [int(x) for x in v2.split(".")]

    # If all numbers are equal, the versions are equal
    if v1_list < v2_list:
        return -1
    if v1_list > v2_list:
        return 1
    return 0


class AppleClangConverter(ClangConverter):
    XCODE_VER_RE = re.compile(r"Xcode\s+((\d+\.?)+)")
    XCODE_BUILD_VER = re.compile(r"Build version\s+(\w+)")
    MIN_VERSION = "11"
    OSX_DEPLOYMENT_TARGET = "11.0"

    def __init__(self, bazel: Bazel):
        super().__init__(bazel)

        verinfo = check_output(["xcodebuild", "-version"]).splitlines()
        version = self.XCODE_VER_RE.match(verinfo[0])[1]
        build = self.XCODE_BUILD_VER.match(verinfo[1])[1]

        xcode_path = check_output(["xcode-select", "-print-path"]).strip()
        sdks = self.parse_xcode_sdks()

        for ver, _ in sdks.get("macOS SDKs", {}).items():
            if compare_versions(ver, self.MIN_VERSION) >= 0:
                self.osx_sdk_root = Path(
                    f"{xcode_path}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX{ver}.sdk"
                )

        logging.debug("OSX: Using Xcode: %s (%s)", version, build)
        logging.debug("OSX: XCode path: %s", self.osx_sdk_root)
        self.filters.append(ApplySysrootFilter(self.osx_sdk_root))

    def parse_xcode_sdks(self):
        """
        Runs and parses the output of 'xcodebuild -showsdks' and return a dictionary of installed SDKs.

        Returns:
            dict: A dictionary containing information about installed SDKs. The dictionary
            is organized by SDK types, and each SDK type contains a mapping of version numbers
            to their corresponding SDK names.
        """

        # Sample output of xcodebuild -showsdks

        # DriverKit SDKs:
        # 	DriverKit 23.0                	-sdk driverkit23.0

        # iOS SDKs:
        # 	iOS 17.0                      	-sdk iphoneos17.0

        # macOS SDKs:
        # 	macOS 14.0                    	-sdk macosx14.0
        # 	macOS 14.0                    	-sdk macosx14.0

        # watchOS SDKs:
        # 	watchOS 10.0                  	-sdk watchos10.0

        output = check_output(["xcodebuild", "-showsdks"])
        sdk_dict = {}
        current_sdk_type = None

        # Split the output into lines and iterate through each line
        lines = output.split("\n")
        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Check for SDK type lines
            # i.e. `macOS SDKs:`
            sdk_type_match = re.match(r"^([a-zA-Z ]+):$", line)
            if sdk_type_match:
                current_sdk_type = sdk_type_match.group(1).strip()
                continue

            # Check for SDK details lines
            # i.e. `macOS 14.0                    	-sdk macosx14.0`
            sdk_match = re.match(r"^([a-zA-Z]+ [\d.]+)\s+-sdk (\S+)$", line)
            if sdk_match and current_sdk_type:
                # Version
                sdk_version = sdk_match.group(1).split()[1]
                # Sdk flag.
                sdk_name = sdk_match.group(2)
                sdk_dict.setdefault(current_sdk_type, {})[sdk_version] = sdk_name

        return sdk_dict
