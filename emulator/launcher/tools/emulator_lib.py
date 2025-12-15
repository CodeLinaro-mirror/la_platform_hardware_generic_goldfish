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

"""Library for launching and managing a Goldfish emulator process."""

import asyncio
import logging
import os
import platform
import random
import re
import shutil
import signal
import sys
import tempfile
from pathlib import Path

from python.runfiles import Runfiles

logging.basicConfig(stream=sys.stderr, encoding="utf-8", level=logging.DEBUG)

class EmulatorLocator:
    """Locates all necessary emulator files and artifacts."""

    def __init__(self, abi, use_zip, tmp_dir):
        self.abi_selection = abi
        self.use_zip = use_zip
        self.tmp_dir = Path(tmp_dir)
        self.r = Runfiles.Create()
        if not self.r:
            raise RuntimeError(
                "Runfiles.Create() failed. This script must be run via 'bazel run' or 'bazel test'."
            )

        self.goldfish_exec = None
        self.system_image_dir = None
        self.phone_ini_path = None
        self.config_ini_path = None
        self.marker_files_path = None
        self.abi = None

    async def find_resources(self):
        """Finds all required resources and raises FileNotFoundError if any are missing."""
        await self._locate_goldfish_exec()
        self._locate_system_image()
        self._locate_ini_files()
        self._locate_marker_files()

    async def _locate_goldfish_exec(self):
        if self.use_zip:
            zip_path = Path(
                self.r.Rlocation("goldfish+/emulator/release.zip")
            )
            if not zip_path.exists():
                raise FileNotFoundError(f"Goldfish zip not found: {zip_path}")

            extract_path = self.tmp_dir / "goldfish"
            if not extract_path.exists():
                extract_path.mkdir()
                process = await asyncio.create_subprocess_exec(
                    "unzip",
                    str(zip_path),
                    "-d",
                    str(extract_path),
                    stdout=asyncio.subprocess.DEVNULL,
                    stderr=asyncio.subprocess.STDOUT,
                )
                await process.wait()
            self.goldfish_exec = extract_path / "emulator"
        else:
            self.goldfish_exec = Path(
                self.r.Rlocation(
                    "goldfish+/emulator/launcher/launcher"
                )
            )

        if platform.system() == "Windows":
            self.goldfish_exec = self.goldfish_exec.with_suffix(".exe")

        if not self.goldfish_exec.exists():
            raise FileNotFoundError(
                f"Goldfish executable not found: {self.goldfish_exec}"
            )

    def _locate_system_image(self):
        abis = (
            ["x86_64", "arm64-v8a"]
            if self.abi_selection == "auto"
            else [self.abi_selection]
        )

        for abi in abis:
            for page_size in ["", "16k"]:
                minigbm_abi_dir = f"minigbm{page_size}-{abi}"
                try:
                    source_prop_path = self.r.Rlocation(
                        f"android_{minigbm_abi_dir}/{abi}/source.properties"
                    )
                    if source_prop_path:
                        system_image_dir = Path(source_prop_path).parent
                        if system_image_dir.exists():
                            logging.info("--- Selected ABI: %s ---", abi)
                            self.abi = abi
                            self.system_image_dir = system_image_dir
                            return
                except Exception:
                    logging.info("Attempt to use %s, failed", abi)

        raise FileNotFoundError("Could not find a valid system image directory.")

    def _locate_ini_files(self):
        minigbm_abi_dir = f"minigbm-{self.abi}"
        self.phone_ini_path = Path(
            self.r.Rlocation(
                f"goldfish+/emulator/sdk/system_images/{minigbm_abi_dir}/phone.ini"
            )
        )
        if not self.phone_ini_path.exists():
            raise FileNotFoundError(f"Phone INI file not found: {self.phone_ini_path}")

        self.config_ini_path = Path(
            self.r.Rlocation(
                f"goldfish+/emulator/sdk/system_images/{minigbm_abi_dir}/phone.avd/config.ini"
            )
        )
        if not self.config_ini_path.exists():
            raise FileNotFoundError(
                f"Config INI file not found: {self.config_ini_path}"
            )

    def _locate_marker_files(self):
        marker_files_path = self.r.Rlocation(
            "goldfish+/emulator/sdk/platforms/empty"
        )
        if not marker_files_path:
            raise FileNotFoundError("Marker files not found.")
        self.marker_files_path = Path(marker_files_path).parent.parent


class EmulatorAvd:
    """Manages the temporary Android Virtual Device (AVD) setup and environment."""

    def __init__(self, locator, base_env, tmp_dir, disable_crash_reporting=False):
        self.locator = locator
        self.base_env = base_env
        self.tmp_dir = tmp_dir
        self.avd_dir = None
        self.disable_crash_reporting = disable_crash_reporting

    def __enter__(self):
        self.avd_dir = tempfile.TemporaryDirectory()
        self._setup_avd()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.avd_dir.cleanup()

    def _setup_avd(self):
        avd_path = Path(self.avd_dir.name)
        shutil.copyfile(self.locator.phone_ini_path, avd_path / "phone.ini")

        ini_dir = avd_path / "phone.avd"
        ini_dir.mkdir()

        with open(self.locator.config_ini_path, "r") as f:
            config_content = f.read()

        # We set the sysdir to the actual location of our system image,
        # android studio will use this to fetch all the source.properties
        # of this image
        config_content += f"\nimage.sysdir.1 = {self.locator.system_image_dir}\n"

        # hardware file that studio will use for the "actual" running config
        with open(ini_dir / "hardware-qemu.ini", "w") as f:
            f.write(config_content)
        with open(ini_dir / "config.ini", "w") as f:
            f.write(config_content)

    def get_environment(self):
        env = self.base_env.copy()
        # Remove bazel-specific env vars if using zip, so emulator doesn't think it's in bazel
        if self.locator.use_zip:
            for e in ["BUILD_WORKING_DIRECTORY", "TEST_BINARY", "RUNFILES_DIR"]:
                env.pop(e, None)

        env.update(
            {
                "ANDROID_HOME": str(self.locator.marker_files_path),
                "ANDROID_TMP": str(self.tmp_dir),
                "ANDROID_AVD_HOME": self.avd_dir.name,
                "ANDROID_EMULATOR_HOME": self.avd_dir.name,
                "ANDROID_EMU_ENABLE_CRASH_REPORTING": (
                    "NO" if self.disable_crash_reporting else "YES"
                ),
            }
        )
        return env


class EmulatorCommand:
    """Builds the command-line arguments for launching the emulator."""

    def __init__(self, locator):
        self.executable = str(locator.goldfish_exec)
        self.args = [self.executable]

    def set_avd(self, name):
        self.args.extend(["-avd", name])
        return self

    def set_sysdir(self, path):
        self.args.extend(["-sysdir", str(path)])
        return self

    def set_ports(self, adb_port, grpc_port):
        self.args.extend(["-port", str(adb_port), "-grpc", str(grpc_port)])
        return self

    def add_arg(self, *args):
        self.args.extend(args)
        return self

    def add_extra_args(self, extra_args):
        if extra_args:
            self.args.extend(extra_args)
        return self

    def get_command(self):
        return self.args


class EmulatorRunner:
    """Handles the execution and monitoring of the emulator process."""

    def __init__(self, command, env):
        self.command = command
        self.env = env
        self.process = None

    async def launch_and_wait(self, timeout, target_log):
        try:
            self.process = await asyncio.create_subprocess_exec(
                *self.command,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                env=self.env,
            )

            return await asyncio.wait_for(
                self._stream_output_and_find_log(self.process.stdout, target_log),
                timeout=timeout,
            )
        finally:
            await self._terminate_process()

    async def _terminate_process(self):
        if self.process and self.process.returncode is None:
            logging.info("--- Terminating emulator process... ---")
            try:
                self.process.terminate()
                await asyncio.wait_for(self.process.wait(), timeout=10)
            except asyncio.TimeoutError:
                logging.warning(
                    "--- Emulator did not terminate gracefully. Forcing kill. ---"
                )
                self.process.kill()
                await self.process.wait()

    async def _stream_output_and_find_log(self, stream, target_log):
        if not target_log:
            await self._stream_output(stream)
            return True

        target_re = re.compile(target_log)
        while True:
            line_bytes = await stream.readline()
            if not line_bytes:
                logging.error("--- Stream ended before target log line was found. ---")
                return False

            line = line_bytes.decode("utf-8", errors="replace").strip()
            logging.info(line)

            if target_re.search(line):
                logging.info("--- Target log line detected! ---")
                return True

    async def _stream_output(self, stream):
        while True:
            line_bytes = await stream.readline()
            if not line_bytes:
                logging.info("--- Emulator process exited. ---")
                break
            line = line_bytes.decode("utf-8", errors="replace").strip()
            logging.info(line)


async def launch_and_monitor_emulator(
    abi,
    use_zip,
    tmp_dir_for_images,
    timeout_seconds,
    target_log_line,
    extra_qemu_args=None,
    disable_crash_reporting=False,
):
    """Launches and monitors an emulator instance.

    Args:
        abi: The ABI to use.
        use_zip: Whether to use the zipped goldfish executable.
        tmp_dir_for_images: Temporary directory for image files.
        timeout_seconds: Timeout for the operation.
        target_log_line: The log line to watch for.
        extra_qemu_args: Additional arguments for the QEMU command.

    Returns:
        0 on success, 1 on failure.
    """
    try:
        locator = EmulatorLocator(abi, use_zip, tmp_dir_for_images)
        await locator.find_resources()

        with EmulatorAvd(
            locator, os.environ, tmp_dir_for_images, disable_crash_reporting
        ) as avd:
            command_builder = EmulatorCommand(locator)
            command_builder.set_avd("phone")
            command_builder.set_sysdir(locator.system_image_dir)
            command_builder.set_ports(
                random.randint(10000, 20000), random.randint(10000, 20000)
            )
            command_builder.add_extra_args(extra_qemu_args)

            command = command_builder.get_command()
            environment = avd.get_environment()

            logging.info(
                "--- Launching emulator for %s with a %d-second timeout... ---",
                locator.abi,
                timeout_seconds,
            )
            logging.info("--- Command: %s ---", " ".join(command))
            logging.info("--- ANDROID_AVD_HOME: %s ---", avd.avd_dir.name)

            runner = EmulatorRunner(command, environment)

            def signal_handler(sig, frame):
                logging.info("Signal received: %d", sig)
                if runner.process:
                    runner.process.send_signal(sig)
            # Note that Bazel forwards SIGINT to all processes so when running
            # under Bazel this might mean the emulator gets signalled twice,
            # which should be fine.
            signal.signal(signal.SIGINT, signal_handler)
            signal.signal(signal.SIGTERM, signal_handler)
            signal.signal(signal.SIGHUP, signal_handler)
            signal.signal(signal.SIGQUIT, signal_handler)

            status = await runner.launch_and_wait(timeout_seconds, target_log_line)

            if not status:
                logging.info("--- Script failed with status: %s. ---", status)
                return 1

            logging.info("--- Script finished successfully. ---")
            return 0

    except (FileNotFoundError, RuntimeError) as e:
        logging.error("--- Configuration error: %s ---", e)
        return 1
    except asyncio.TimeoutError:
        logging.error(
            "--- FAILED: Did not see log line within %d seconds. ---",
            timeout_seconds,
        )
        return 1
    except Exception as e:
        logging.error("--- An unexpected error occurred: %s ---", e)
        return 1
