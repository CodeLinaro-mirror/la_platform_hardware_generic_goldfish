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

"""Tests that the emulator can start the bootprocess and can launch the kernel."""

import sys
import os
import logging
import asyncio
import argparse
from pathlib import Path
from python.runfiles import Runfiles
import re


async def stream_output_and_find_log(stream, target_log):
    """
    Asynchronously reads from a stream, prints each line, and returns on finding the target.
    """
    target_re = re.compile(target_log)
    while True:
        line_bytes = await stream.readline()
        if not line_bytes:
            # End of stream reached before finding the log
            logging.error("--- Stream ended before target log line was found. ---")
            return False

        line = line_bytes.decode("utf-8", errors="replace").strip()
        print(line)  # Print emulator output in real-time

        if target_re.search(line):
            logging.info("--- Target log line detected! ---")
            return True


async def main(args):
    r = Runfiles.Create()
    if not r:
        logging.error(
            "Error: Runfiles.Create() failed. This script must be run via 'bazel run' or 'bazel test'."
        )
        return 1

    # --- Dynamically locate necessary files based on ABI ---
    try:
        minigbm_abi_dir = f"minigbm-{args.abi}"

        goldfish_exec = Path(
            r.Rlocation("_main/hardware/generic/goldfish/emulator/launcher/goldfish")
        )
        if not goldfish_exec.exists():
            raise FileNotFoundError(f"Goldfish executable not found: {goldfish_exec}")

        system_image_dir = Path(
            r.Rlocation(f"android_{minigbm_abi_dir}/{args.abi}/source.properties")
        ).parent
        if not system_image_dir.exists():
            raise FileNotFoundError(
                f"System image directory not found: {system_image_dir}"
            )

        phone_ini_path = Path(
            r.Rlocation(
                f"_main/hardware/generic/goldfish/emulator/sdk/system_images/{minigbm_abi_dir}/phone.ini"
            )
        )
        if not phone_ini_path.exists():
            raise FileNotFoundError(f"Phone INI file not found: {phone_ini_path}")

    except Exception as e:
        logging.error(
            "--- Error locating necessary runfiles for ABI '%s': %s ---", args.abi, e
        )
        logging.error(
            "--- Please ensure the runfiles for the selected ABI are available in your build. ---"
        )
        return 1

    # --- Prepare the command ---
    command_to_run = [
        str(goldfish_exec),
        "-avd",
        "phone",
        "-sysdir",
        str(system_image_dir),
        "-verbose",
        "-vmodule",
        "*=1",
        "-show-kernel",
    ]

    # Set required environment variables
    avd_parent_dir = str(phone_ini_path.parent)
    env = {
        **os.environ,
        "ANDROID_AVD_HOME": avd_parent_dir,
        "ANDROID_EMULATOR_HOME": avd_parent_dir,
    }

    logging.info(
        "--- Launching emulator for %s with a %d-second timeout... ---",
        args.abi,
        args.timeout_seconds,
    )

    process = None
    try:
        # Start the emulator process asynchronously
        process = await asyncio.create_subprocess_exec(
            *command_to_run,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
            env=env,
        )

        # Wait for the log-finding task to complete, with a timeout
        await asyncio.wait_for(
            stream_output_and_find_log(process.stdout, args.target_log_line),
            timeout=args.timeout_seconds,
        )

        logging.info("--- Script finished successfully. ---")
        return 0  # Success

    except asyncio.TimeoutError:
        logging.error(
            "--- FAILED: Did not see log line within %d seconds. ---",
            args.timeout_seconds,
        )
        return 1  # Failure
    except Exception as e:
        logging.error("--- An unexpected error occurred: %s ---", e)
        return 1  # Failure
    finally:
        if process and process.returncode is None:
            logging.info("--- Terminating emulator process... ---")
            try:
                process.terminate()
                await asyncio.wait_for(process.wait(), timeout=10)
            except asyncio.TimeoutError:
                logging.warning(
                    "--- Emulator did not terminate gracefully. Forcing kill. ---"
                )
                process.kill()


if __name__ == "__main__":

    # --- Argument Parsing ---
    parser = argparse.ArgumentParser(
        description="Launch a Goldfish emulator and wait for a boot message."
    )
    parser.add_argument(
        "--abi",
        type=str,
        choices=["x86_64", "arm64-v8a"],
        default="x86_64",
        help="The ABI of the system image to run. Defaults to x86_64.",
    )
    parser.add_argument(
        "--target_log_line",
        type=str,
        default="Linux version 6\.6\.66-android15-8-gb66429556fb8-ab13070261",
        help="The script will exit successfully when a log line matching this regex is detected.",
    )
    parser.add_argument(
        "--timeout_seconds",
        type=int,
        default=30,
        help="Timeout in seconds to wait for the target log line.",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=0,
        help="Number of times to repeat this test.",
    )

    args = parser.parse_args()
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(levelname)s - %(message)s",
        stream=sys.stdout,
    )

    logging.info("--- Selected ABI: %s ---", args.abi)

    for _ in range(args.repeat + 1):
        exit_code = asyncio.run(main(args))
        if exit_code != 0:
            sys.exit(exit_code)
