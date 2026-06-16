# Copyright 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

from emulator.tools.symbol_zipper import (
    symbol_destination,
)
from python.runfiles import Runfiles


def main():
    r = Runfiles.Create()
    if not r:
        print("Failed to initialize runfiles", file=sys.stderr)
        sys.exit(1)

    exe_suffix = ".exe" if sys.platform == "win32" else ""
    crashreport_path = r.Rlocation(f"goldfish+/emulator/crashreport/tool/crashreport{exe_suffix}")
    if not crashreport_path:
        print("Failed to locate crashreport binary", file=sys.stderr)
        sys.exit(1)

    args_to_forward = [arg if arg != "-h" else "--help" for arg in sys.argv[1:]]
    has_help = any(arg in ["--help", "--helpfull"] for arg in args_to_forward)
    has_minidump = any(arg.startswith("--minidump") for arg in args_to_forward)
    if has_minidump and not has_help:
        with TemporaryDirectory("sym") as tm:
            runfiles_root = Path(r.Rlocation("goldfish+"))
            for f in runfiles_root.rglob("*.sym"):
                fl = Path(tm) / symbol_destination(f)
                fl.parent.mkdir(parents=True, exist_ok=True)
                fl.symlink_to(f)

            cmd = [crashreport_path] + args_to_forward + [f"--symbol_paths={tm}"]
            ret = execute_crashreport(cmd)
    else:
        cmd = [crashreport_path] + args_to_forward
        ret = execute_crashreport(cmd)

    if ret != 0:
        if has_help and ret == 1:
            sys.exit(0)
        if not has_help:
            print(f"Error executing crashreport: {ret}", file=sys.stderr)
        sys.exit(ret)


def execute_crashreport(cmd):
    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        # Stream stdout
        if process.stdout:
            for line in iter(process.stdout.readline, ""):
                print(line, end="")

        # Stream stderr
        if process.stderr:
            for line in iter(process.stderr.readline, ""):
                print(line, end="", file=sys.stderr)

        process.wait()
        return process.returncode

    except Exception as e:
        print(f"Error executing crashreport: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    main()
