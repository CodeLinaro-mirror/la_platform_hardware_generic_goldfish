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

"""Command-line entry point for launching a Goldfish emulator."""

import sys
import logging
import asyncio
import argparse
import tempfile
import time
from emulator_lib import launch_and_monitor_emulator


if __name__ == "__main__":
    # --- Argument Parsing ---
    # We use parse_known_args to allow passing arbitrary flags through to the emulator.
    parser = argparse.ArgumentParser(
        description="Launch a Goldfish emulator and wait for a boot message."
    )
    parser.add_argument(
        "--abi",
        type=str,
        choices=["x86_64", "arm64-v8a", "auto"],
        default="auto",
        help="The ABI of the system image to run. Defaults to auto derive.",
    )
    parser.add_argument(
        "--target_log_line",
        type=str,
        default="-- THESE ARE NOT THE DROIDS YOU ARE LOOKING FOR --",
        help="The script will exit successfully when a log line matching this regex is detected.",
    )
    parser.add_argument(
        "--timeout_seconds",
        type=int,
        default=sys.maxsize,
        help="Timeout in seconds to wait for the target log line.",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=0,
        help="Number of times to repeat this test.",
    )
    parser.add_argument(
        "--disable-crash-reporting",
        action="store_true",
        help="Disable the crash reporting engine.",
    )
    parser.add_argument(
        "--use_zip",
        action="store_true",
        help="Use the goldfish from the release zip",
    )
    parser.add_argument(
        "--system_image_dir",
        help="Use the this system image directory instead of the default.",
    )

    args, extra_args = parser.parse_known_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(levelname)s - %(message)s",
        stream=sys.stdout,
    )

    # The emulator expects '--' to be stripped if it's present.
    if extra_args and extra_args[0] == "--":
        extra_args = extra_args[1:]

    with tempfile.TemporaryDirectory() as tmp_dir_for_images:
        for i in range(args.repeat + 1):
            if args.repeat > 0:
                logging.info(
                    "--- Running iteration %d of %d ---", i + 1, args.repeat + 1
                )
            exit_code = asyncio.run(
                launch_and_monitor_emulator(
                    abi=args.abi,
                    use_zip=args.use_zip,
                    tmp_dir_for_images=tmp_dir_for_images,
                    timeout_seconds=args.timeout_seconds,
                    target_log_line=args.target_log_line,
                    extra_qemu_args=extra_args,
                    disable_crash_reporting=args.disable_crash_reporting,
                    system_image_dir=args.system_image_dir,
                )
            )

            if exit_code != 0:
                sys.exit(exit_code)

            # give netsim some time to die
            time.sleep(5)
