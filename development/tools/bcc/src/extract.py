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
import multiprocessing
import os
from pathlib import Path

try:
    from tqdm import tqdm
except ImportError:

    def tqdm(iterable, *args, **kwargs):
        """Pass-through stub for tqdm if not available

        We print a . as an indicator we are doing things.
        """
        for item in iterable:
            print(".", end="", flush=True)
            yield item
        print()  # Print a newline at the end


from aemu.converter.converter import Converter
from aemu.log import configure_logging


class CustomFormatter(
    argparse.RawTextHelpFormatter, argparse.ArgumentDefaultsHelpFormatter
):
    pass


def compile_commands(targets, args):
    compile_command_entries = []
    converter = Converter(args.cwd, args.aosp)

    # Let's calculate the closure
    closure = set()
    logging.warning("Calculating closure")
    for target in targets:
        closure = closure.union(converter.bazel.closure(target))

    # And map reduce the results..
    logging.warning("Processing items with %s threads, this can take a while", multiprocessing.cpu_count())
    with multiprocessing.Pool(processes=multiprocessing.cpu_count()) as pool:
        entries = tqdm(pool.map(converter.convert_target, targets))

    # Flatten
    compile_command_entries = [x for xs in entries for x in xs]

    # And write out
    if args.out:
        dest = Path(args.out).absolute()
        if dest.is_dir():
            dest = dest / "compile_commands.json"

        logging.warning("Writing %s", dest)
        with open(dest, "w") as fb:
            json.dump(compile_command_entries, fb, indent=2)
    else:
        print(json.dumps(compile_command_entries, indent=2))


def fix_bazel_args(args):
    logging.warning("Running the extractor in a bazel workspace.")
    if os.environ["BUILD_WORKSPACE_DIRECTORY"] != args.cwd:
        logging.warning("and using -C, this might not work")
    if not Path(args.out).is_absolute():
        args.out = Path(os.environ["BUILD_WORKING_DIRECTORY"]) / args.out


def is_bazel_run():
    return "BUILD_WORKSPACE_DIRECTORY" in os.environ


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
        "--aosp", help="Optional aosp root."
    )

    parser.add_argument(
        "-C",
        dest="cwd",
        default=os.environ.get("BUILD_WORKSPACE_DIRECTORY", os.getcwd()),
        help=(
            "Set the working directory, should be inside your bazel "
            "workspace. Do not use this when running from `bazel run`",
        ),
    )

    parser.add_argument(
        "-o",
        "--out",
        dest="out",
        default=Path(os.environ.get("BUILD_WORKING_DIRECTORY", ""))
        / "compile_commands.json",
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

    if is_bazel_run():
        fix_bazel_args(args)

    if args.targets:
        compile_commands(args.targets, args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
