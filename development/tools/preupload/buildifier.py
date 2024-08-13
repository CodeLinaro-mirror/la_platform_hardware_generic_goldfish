#!/usr/bin/env python3
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
import os
import subprocess
import sys
import shutil


def main():
    if not shutil.which("buildifier"):
        print("Buildifier not installed, ignoring test")
        return 0


    # Get the list of files from the command line arguments
    files = [x for x in sys.argv[1:] if is_bazel_file(x)]
    if not files:
        print("No Bazel files found, ignoring test")
        return 0

    s = subprocess.check_call(["buildifier", "-mode=fix"] + files)
    print(" ".join(["buildifier", "-mode=fix"] + files))
    sys.exit(1)


def is_bazel_file(file_path):
    """Checks if the given file path corresponds to a Bazel file.

    Args:
        file_path: The path to the file.

    Returns:
        True if the file is named 'BUILD' or 'BUILD.bazel', False otherwise.
    """
    return os.path.basename(file_path) in ("BUILD", "BUILD.bazel")


if __name__ == "__main__":
    main()
