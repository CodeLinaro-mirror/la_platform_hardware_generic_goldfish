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
import socket
import sys
import tempfile
from pathlib import Path

from python.runfiles import Runfiles
from process_monitor import ProcessTreeMonitor

logging.basicConfig(stream=sys.stderr, encoding="utf-8", level=logging.DEBUG)


def find_available_port(start_port=8085, host="127.0.0.1", max_attempts=100):
    """Finds the first available TCP port starting from `start_port`.

    Args:
        start_port: The port to begin scanning from (default: 8085).
        host: The host interface to probe (default: '127.0.0.1').
        max_attempts: Maximum number of sequential ports to probe.

    Returns:
        An available port number.

    Raises:
        RuntimeError: If no available port could be found within `max_attempts`.
    """
    try:
        addrinfo = socket.getaddrinfo(
            host, None, family=socket.AF_UNSPEC, type=socket.SOCK_STREAM, flags=socket.AI_PASSIVE
        )
        family = addrinfo[0][0] if addrinfo else socket.AF_INET
    except OSError:
        family = socket.AF_INET

    for port in range(start_port, start_port + max_attempts):
        try:
            with socket.socket(family, socket.SOCK_STREAM) as s:
                s.bind((host, port))
                return port
        except OSError:
            continue
    raise RuntimeError(
        f"Unable to find an available port in range [{start_port}, {start_port + max_attempts}) on {host}."
    )


class EmulatorLocator:
    """Locates all necessary emulator files and artifacts."""

    def __init__(self, abi, use_zip, tmp_dir, system_image_dir):
        self.abi_selection = abi
        self.use_zip = use_zip
        self.tmp_dir = Path(tmp_dir)
        self.r = Runfiles.Create()
        if not self.r:
            raise RuntimeError(
                "Runfiles.Create() failed. This script must be run via 'bazel run' or 'bazel test'."
            )

        self.goldfish_exec = None
        self.aquarium_exec = None
        self.system_image_dir = system_image_dir
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
        self._locate_aquarium_ui()

    def _locate_aquarium_ui(self):
        aquarium_rlocation = "goldfish+/emulator/tools/aquarium-ui/aquarium-ui"
        aquarium_path = self.r.Rlocation(aquarium_rlocation)
        if not aquarium_path:
            raise FileNotFoundError(
                f"Couldn't find the aquarium ui binary in runfiles: {aquarium_rlocation}"
            )
        self.aquarium_exec = Path(aquarium_path)
        if platform.system() == "Windows":
            self.aquarium_exec = self.aquarium_exec.with_suffix(".exe")

        if not self.aquarium_exec.exists():
            raise FileNotFoundError(
                f"Couldn't find the aquarium ui binary: {self.aquarium_exec}"
            )
        if self.aquarium_exec.is_dir():
            raise IsADirectoryError(
                f"Aquarium UI binary is a directory, not a file: {self.aquarium_exec}"
            )
        if not self.aquarium_exec.is_file() or not os.access(
            self.aquarium_exec, os.X_OK
        ):
            raise PermissionError(
                f"Couldn't access the aquarium ui binary: {self.aquarium_exec}"
            )

    async def _locate_goldfish_exec(self):
        if self.use_zip:
            zip_path = Path(self.r.Rlocation("goldfish+/emulator/release.zip"))
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
            self.goldfish_exec = extract_path / "emulator" / "emulator"
        else:
            self.goldfish_exec = Path(
                self.r.Rlocation("goldfish+/emulator/launcher/emulator")
            )

        if platform.system() == "Windows":
            self.goldfish_exec = self.goldfish_exec.with_suffix(".exe")

        if not self.goldfish_exec.exists():
            raise FileNotFoundError(
                f"Goldfish executable not found: {self.goldfish_exec}"
            )

    def _locate_system_image(self):
        if self.system_image_dir:
            self.abi = Path(self.system_image_dir).name
            logging.info("--- Using local image: %s ---", self.system_image_dir)
            return

        abis = (
            ["x86_64", "arm64-v8a"]
            if self.abi_selection == "auto"
            else [self.abi_selection]
        )

        for abi in abis:
            for page_size in ["", "16k"]:
                minigbm_abi_dir = f"{page_size}-{abi}"
                try:
                    source_prop_path = self.r.Rlocation(
                        f"android{minigbm_abi_dir}/{abi}/source.properties"
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
        marker_files_path = self.r.Rlocation("goldfish+/emulator/sdk/platforms/empty")
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

        # Fishtank (qemu-now) uses the following logic to get the full path to phone.avd:
        # 1) Check $ANDROID_AVD_HOME/../<`path.rel` in phone.ini>. If it does not exist, then
        # 2) Check the absolute path provided by `path` in phone.ini.
        #
        # In <avd_name>.ini files provided by the Android Studio sdk, where
        # ANDROID_AVD_HOME=~/.android/avd, those path fields would typically be, for example:
        #
        # == my_avd.ini =============
        # avd.ini.encoding=UTF-8
        # path.rel=avd/my_avd.avd
        # path=<abs-path-to>/my_avd.avd
        # target=android-36
        # ===========================
        #
        # So to workaround this strange logic in qemu-now, we inject the absolute path to phone.avd
        # in phone.ini, in case fishtank can't find it with path.rel.
        with open(avd_path / "phone.ini", "a") as f:
            f.write(f"\npath = {ini_dir}\n")

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
    """Handles the execution and monitoring of the emulator and auxiliary processes."""

    def __init__(
        self,
        command,
        aquarium_exec,
        env,
        http_port=8085,
        http_address="127.0.0.1",
    ):
        self.command = command
        self.aquarium_exec = aquarium_exec
        self.env = env
        self.http_port = http_port
        self.http_address = http_address
        self.process = None
        self.aquarium = None
        self.auxiliary_processes = []
        self.background_tasks = []
        self._discovery_line = re.compile(r"Advertising in discovery file: (.*)$")

    def register_process(self, proc, name):
        """Registers an auxiliary subprocess to be gracefully terminated on teardown."""
        self.auxiliary_processes.append((proc, name))

    def start_background_task(self, coro):
        """Starts a background coroutine as an asyncio task and tracks it for cleanup."""
        task = asyncio.create_task(coro)
        self.background_tasks.append(task)
        return task

    async def launch_and_wait(
        self,
        timeout,
        target_log,
        count_log_pattern=None,
        expected_occurrences=None,
    ):
        monitor = None
        try:
            self.process = await asyncio.create_subprocess_exec(
                *self.command,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                env=self.env,
            )

            if self.process and self.process.pid:
                monitor = ProcessTreeMonitor(self.process.pid, sample_interval_sec=1.0)
                await monitor.start()

            try:
                status = await asyncio.wait_for(
                    self._stream_output_and_find_log(
                        self.process.stdout,
                        target_log,
                        count_log_pattern,
                        expected_occurrences,
                    ),
                    timeout=timeout,
                )
            except asyncio.TimeoutError:
                if monitor:
                    summary = await monitor.stop()
                    logging.error(
                        "\n%s",
                        monitor.format_diagnostic_dump(
                            summary, reason=f"Timeout ({timeout}s)"
                        ),
                    )
                raise

            if monitor:
                summary = await monitor.stop()
                if status:
                    logging.info(
                        monitor.format_summary_line(summary, tag="Boot Telemetry")
                    )
                else:
                    logging.error(
                        "\n%s",
                        monitor.format_diagnostic_dump(
                            summary, reason="Stream ended before target log line"
                        ),
                    )

            return status
        finally:
            if monitor:
                await monitor.stop()
            await self._terminate_process()

    async def _pump_stream(self, stream, tag, on_line=None):
        """Asynchronously pumps lines from a stream to logging with a tag prefix."""
        while True:
            line_bytes = await stream.readline()
            if not line_bytes:
                logging.info("[%s] --- Process stream closed ---", tag)
                break
            line = line_bytes.decode("utf-8", errors="replace").strip()
            logging.info("[%s] %s", tag, line)
            if on_line:
                await on_line(line)

    async def _terminate_process(self):
        # Cancel all background streaming tasks
        for task in self.background_tasks:
            if not task.done():
                task.cancel()
        if self.background_tasks:
            await asyncio.gather(*self.background_tasks, return_exceptions=True)

        for proc, name in [(self.process, "emulator")] + self.auxiliary_processes:
            if proc and proc.returncode is None:
                logging.info("--- Terminating %s process... ---", name)
                try:
                    proc.terminate()
                    await asyncio.wait_for(proc.wait(), timeout=10)
                except asyncio.TimeoutError:
                    logging.warning(
                        "--- %s did not terminate gracefully. Forcing kill. ---", name
                    )
                    proc.kill()
                    await proc.wait()

    async def _on_emulator_line(self, line):
        discovery_match = self._discovery_line.search(line)
        if discovery_match and self.aquarium is None:
            discovery_file = discovery_match.group(1).strip()
            logging.info("--- Found discovery file: %s ---", discovery_file)
            await self._launch_aquarium(discovery_file)

    async def _launch_aquarium(self, discovery_file):
        if not self.aquarium_exec:
            return

        # Note: There's a brief period where port could be claimed by someone else.
        port = await asyncio.to_thread(
            find_available_port,
            start_port=self.http_port,
            host=self.http_address,
        )
        if port != self.http_port:
            logging.info(
                "--- Port %d was in use; found available port %d for Aquarium UI ---",
                self.http_port,
                port,
            )

        aquarium_cmd = [
            str(self.aquarium_exec),
            "--discovery_file",
            discovery_file,
            "--http_address",
            self.http_address,
            "--http_port",
            str(port),
        ]
        logging.info("Launching: %s", " ".join(aquarium_cmd))
        try:
            self.aquarium = await asyncio.create_subprocess_exec(
                *aquarium_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                env=self.env,
            )
            self.register_process(self.aquarium, "aquarium")
            self.start_background_task(
                self._pump_stream(self.aquarium.stdout, tag="aquarium")
            )
        except Exception as e:
            logging.error("--- Could not start aquarium: %s ---", e)
            raise

    async def _stream_output_and_find_log(
        self,
        stream,
        target_log,
        count_log_pattern=None,
        expected_occurrences=None,
    ):
        if not target_log:
            await self._stream_output(stream)
            return True

        target_re = re.compile(target_log)
        count_re = re.compile(count_log_pattern) if count_log_pattern else None
        pattern_count = 0

        while True:
            line_bytes = await stream.readline()
            if not line_bytes:
                logging.error(
                    "[emulator] --- Stream ended before target log line was found. ---"
                )
                return False

            line = line_bytes.decode("utf-8", errors="replace").strip()
            logging.info("[emulator] %s", line)

            if count_re and count_re.search(line):
                pattern_count += 1
                logging.info(
                    "--- Matched count_log_pattern '%s' (current count=%d) ---",
                    count_log_pattern,
                    pattern_count,
                )

            await self._on_emulator_line(line)

            if target_re.search(line):
                logging.info("--- Target log line detected! ---")
                if (
                    expected_occurrences is not None
                    and pattern_count != expected_occurrences
                ):
                    logging.error(
                        "--- Assertion failed: count_log_pattern '%s' occurred %d times, expected exactly %d ---",
                        count_log_pattern,
                        pattern_count,
                        expected_occurrences,
                    )
                    return False
                return True

    async def _stream_output(self, stream):
        await self._pump_stream(stream, tag="emulator", on_line=self._on_emulator_line)


async def launch_and_monitor_emulator(
    abi,
    use_zip,
    tmp_dir_for_images,
    timeout_seconds,
    target_log_line,
    extra_qemu_args=None,
    disable_crash_reporting=False,
    system_image_dir=None,
    count_log_pattern=None,
    expected_occurrences=None,
    http_port=8085,
    http_address="127.0.0.1",
):
    """Launches and monitors an emulator instance.

    Args:
        abi: The ABI to use.
        use_zip: Whether to use the zipped goldfish executable.
        tmp_dir_for_images: Temporary directory for image files.
        timeout_seconds: Timeout for the operation.
        target_log_line: The log line to watch for.
        extra_qemu_args: Additional arguments for the QEMU command.
        system_image_dir: Optional path to a system image to use.
        count_log_pattern: Regular expression pattern to count in emulator logs.
        expected_occurrences: Exact number of times count_log_pattern must occur.
        http_port: Starting port to scan for Aquarium UI (default: 8085).
        http_address: Host interface to bind Aquarium UI on (default: '127.0.0.1').

    Returns:
        0 on success, 1 on failure.
    """
    try:
        locator = EmulatorLocator(abi, use_zip, tmp_dir_for_images, system_image_dir)
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

            runner = EmulatorRunner(
                command,
                locator.aquarium_exec,
                environment,
                http_port=http_port,
                http_address=http_address,
            )

            def signal_handler(sig, frame):
                logging.info("Signal received: %d", sig)
                if runner.process:
                    runner.process.send_signal(sig)

            # Note that Bazel forwards SIGINT to all processes so when running
            # under Bazel this might mean the emulator gets signaled twice,
            # which should be fine.
            signal.signal(signal.SIGINT, signal_handler)
            signal.signal(signal.SIGTERM, signal_handler)
            if platform.system() == "Windows":
                signal.signal(signal.SIGBREAK, signal_handler)
            else:
                signal.signal(signal.SIGHUP, signal_handler)
                signal.signal(signal.SIGQUIT, signal_handler)

            status = await runner.launch_and_wait(
                timeout_seconds,
                target_log_line,
                count_log_pattern,
                expected_occurrences,
            )

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
