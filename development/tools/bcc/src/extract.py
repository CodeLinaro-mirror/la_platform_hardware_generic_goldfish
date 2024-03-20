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
import argparse
import json
import logging
import os
from pathlib import Path

try:
    from tqdm import tqdm
except ImportError:

    def tqdm(iterable, *args, **kwargs):
        """Pass-through stub for tqdm if not available"""
        return iterable


from aemu.converter.converter import Converter
from aemu.log import configure_logging


class CustomFormatter(
    argparse.RawTextHelpFormatter, argparse.ArgumentDefaultsHelpFormatter
):
    pass


def compile_commands(targets, args):
    compile_command_entries = []
    converter = Converter(args.cwd, args.aosp)
    closure = set()
    for target in targets:
        closure = closure.union(converter.bazel.closure(target))

    for target in tqdm(closure):
        compile_command_entries += converter.convert_target(target)

    if args.out:
        dest = Path(args.out)
        if dest.is_dir():
            dest = dest / "compile_commands.json"

        with open(dest, "w") as fb:
            json.dump(compile_command_entries, fb, indent=2)
    else:
        print(json.dumps(compile_command_entries, indent=2))


def main():
    parser = argparse.ArgumentParser(
        formatter_class=CustomFormatter,
        description="""
        Bazel Compile Commands generator

        This will generate a compile_commands.json that you can use with your
        favorite ide.

        This is heavily tailored towards AOSP, and might not work outside
        aosp.
        """,
    )

    parser.add_argument(
        "--aosp", help="Aosp root, used to derive bazel if it is not on the path"
    )

    parser.add_argument(
        "-C",
        dest="cwd",
        default=os.environ.get("BUILD_WORKSPACE_DIRECTORY", os.getcwd()),
        help="Set the working directory, should be inside your bazel workspace",
    )

    parser.add_argument(
        "-o",
        "--out",
        dest="out",
        default="compile_commands.json",
        help="Set the output file or directory",
    )

    parser.add_argument(
        "-v",
        "--verbose",
        dest="verbose",
        default=False,
        action="store_true",
        help="Verbose logging",
    )

    parser.add_argument("targets", nargs="+", help="List of targets")
    args = parser.parse_args()

    lvl = logging.DEBUG if args.verbose else logging.INFO
    configure_logging(lvl)

    if args.targets:
        compile_commands(args.targets, args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
