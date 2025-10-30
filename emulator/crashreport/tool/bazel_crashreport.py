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
import subprocess
import sys
from tempfile import TemporaryDirectory
import os
import argparse
from hardware.generic.goldfish.emulator.tools.symbol_zipper import (
    symbol_destination,
    is_symbol_file,
)
from python.runfiles import Runfiles
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description='Decode crash reports.')
    parser.add_argument('-l', action='store_true', help='List local crash reports')
    parser.add_argument('-u', action='store_true', help='Upload local crash reports')
    parser.add_argument('-e', action='store_true', help='Erase local crash reports')
    parser.add_argument('-d', type=str, help='Process the given minidump file')
    parser.add_argument('-m', action='store_true', help='Output in machine-readable format')
    parser.add_argument('-s', action='store_true', help='Output stack contents')
    parser.add_argument('--symbol_paths', type=str, nargs='*', help='Paths to symbol files')

    args, unknownargs = parser.parse_known_args()

    r = Runfiles.Create()
    crashreport_path = r.Rlocation(
        "_main/hardware/generic/goldfish/emulator/crashreport/tool/crashreport"
    )

    cmd = [crashreport_path]
    if args.l:
        cmd.append('-l')
    if args.u:
        cmd.append('-u')
    if args.e:
        cmd.append('-e')
    if args.m:
        cmd.append('-m')
    if args.s:
        cmd.append('-s')
    if args.symbol_paths:
        cmd.extend(['--symbol_paths'] + args.symbol_paths)

    if args.d:
        cmd.extend(['-d', args.d])
        with TemporaryDirectory("sym") as tm:
            runfiles_root = Path(r.Rlocation("_main"))
            for f in runfiles_root.rglob("*.sym"):
                fl = Path(tm) / symbol_destination(f)
                fl.parent.mkdir(parents=True, exist_ok=True)
                fl.symlink_to(f)
            cmd.append(tm)
            execute_crashreport(cmd)
    else:
        cmd.extend(unknownargs)
        execute_crashreport(cmd)

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
            for line in iter(process.stdout.readline, ''):
                print(line, end='')

        # Stream stderr
        if process.stderr:
            for line in iter(process.stderr.readline, ''):
                print(line, end='', file=sys.stderr)

        process.wait()
        if process.returncode != 0:
            print(f"Error executing crashreport: {process.returncode}", file=sys.stderr)
            sys.exit(process.returncode)

    except Exception as e:
        print(f"Error executing crashreport: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
