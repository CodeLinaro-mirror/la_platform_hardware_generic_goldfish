# -*- coding: utf-8 -*-
# Copyright 2026 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Unit tests for emulator_lib."""

import asyncio
from pathlib import Path
import socket
import unittest
from unittest.mock import AsyncMock, patch

from emulator_lib import EmulatorLocator, EmulatorRunner, find_available_port


class EmulatorLocatorTest(unittest.TestCase):
    """Tests for EmulatorLocator resource detection."""

    def setUp(self):
        self.mock_runfiles = unittest.mock.MagicMock()
        patcher = patch("emulator_lib.Runfiles.Create", return_value=self.mock_runfiles)
        self.addCleanup(patcher.stop)
        patcher.start()
        self.locator = EmulatorLocator("x86_64", False, "/tmp", None)

    @patch("platform.system", return_value="Linux")
    @patch("os.access", return_value=True)
    @patch.object(Path, "is_file", return_value=True)
    @patch.object(Path, "is_dir", return_value=False)
    @patch.object(Path, "exists", return_value=True)
    def test_locate_aquarium_ui_success(
        self, mock_exists, mock_is_dir, mock_is_file, mock_access, mock_system
    ):
        self.mock_runfiles.Rlocation.return_value = "/mock/aquarium-ui"
        self.locator._locate_aquarium_ui()
        self.assertEqual(self.locator.aquarium_exec, Path("/mock/aquarium-ui"))

    @patch("platform.system", return_value="Windows")
    @patch("os.access", return_value=True)
    @patch.object(Path, "is_file", return_value=True)
    @patch.object(Path, "is_dir", return_value=False)
    @patch.object(Path, "exists", return_value=True)
    def test_locate_aquarium_ui_windows_appends_exe(
        self, mock_exists, mock_is_dir, mock_is_file, mock_access, mock_system
    ):
        self.mock_runfiles.Rlocation.return_value = "/mock/aquarium-ui"
        self.locator._locate_aquarium_ui()
        self.assertEqual(self.locator.aquarium_exec, Path("/mock/aquarium-ui.exe"))

    def test_locate_aquarium_ui_not_in_runfiles(self):
        self.mock_runfiles.Rlocation.return_value = None
        with self.assertRaises(FileNotFoundError) as ctx:
            self.locator._locate_aquarium_ui()
        self.assertIn("aquarium-ui", str(ctx.exception))

    @patch("platform.system", return_value="Linux")
    @patch.object(Path, "exists", return_value=False)
    def test_locate_aquarium_ui_missing_file(self, mock_exists, mock_system):
        self.mock_runfiles.Rlocation.return_value = "/mock/missing-aquarium-ui"
        with self.assertRaises(FileNotFoundError) as ctx:
            self.locator._locate_aquarium_ui()
        self.assertIn("/mock/missing-aquarium-ui", str(ctx.exception))

    @patch("platform.system", return_value="Linux")
    @patch.object(Path, "is_dir", return_value=True)
    @patch.object(Path, "exists", return_value=True)
    def test_locate_aquarium_ui_is_directory(self, mock_exists, mock_is_dir, mock_system):
        self.mock_runfiles.Rlocation.return_value = "/mock/dir-aquarium-ui"
        with self.assertRaises(IsADirectoryError) as ctx:
            self.locator._locate_aquarium_ui()
        self.assertIn("/mock/dir-aquarium-ui", str(ctx.exception))

    @patch("platform.system", return_value="Linux")
    @patch("os.access", return_value=False)
    @patch.object(Path, "is_file", return_value=True)
    @patch.object(Path, "is_dir", return_value=False)
    @patch.object(Path, "exists", return_value=True)
    def test_locate_aquarium_ui_not_executable(
        self, mock_exists, mock_is_dir, mock_is_file, mock_access, mock_system
    ):
        self.mock_runfiles.Rlocation.return_value = "/mock/noexec-aquarium-ui"
        with self.assertRaises(PermissionError) as ctx:
            self.locator._locate_aquarium_ui()
        self.assertIn("/mock/noexec-aquarium-ui", str(ctx.exception))


class EmulatorLibTest(unittest.IsolatedAsyncioTestCase):
    """Tests for emulator_lib helpers and runner."""

    def test_find_available_port_returns_port(self):
        port = find_available_port(start_port=8085, host="127.0.0.1")
        self.assertGreaterEqual(port, 8085)

    def test_find_available_port_skips_occupied_port(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            occupied_port = s.getsockname()[1]
            s.listen(1)

            next_port = find_available_port(start_port=occupied_port, host="127.0.0.1")
            self.assertGreater(next_port, occupied_port)

    def test_find_available_port_exhaustion_raises(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            occupied_port = s.getsockname()[1]
            s.listen(1)

            with self.assertRaises(RuntimeError):
                find_available_port(
                    start_port=occupied_port, host="127.0.0.1", max_attempts=1
                )

    def test_find_available_port_ipv6_returns_port(self):
        if not socket.has_ipv6:
            self.skipTest("IPv6 not supported on this platform")
        try:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.bind(("::1", 0))
        except OSError:
            self.skipTest("IPv6 loopback '::1' not available")

        port = find_available_port(start_port=8085, host="::1")
        self.assertGreaterEqual(port, 8085)

    def test_find_available_port_ipv6_skips_occupied_port(self):
        if not socket.has_ipv6:
            self.skipTest("IPv6 not supported on this platform")
        try:
            with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
                s.bind(("::1", 0))
                occupied_port = s.getsockname()[1]
                s.listen(1)

                next_port = find_available_port(start_port=occupied_port, host="::1")
                self.assertGreater(next_port, occupied_port)
        except OSError:
            self.skipTest("IPv6 loopback '::1' not available")

    @patch("socket.socket")
    def test_find_available_port_uses_af_inet6_for_ipv6(self, mock_socket_cls):
        if not socket.has_ipv6:
            self.skipTest("IPv6 not supported on this platform")
        try:
            addrinfo = socket.getaddrinfo(
                "::1", None, family=socket.AF_UNSPEC, type=socket.SOCK_STREAM, flags=socket.AI_PASSIVE
            )
            if not addrinfo or addrinfo[0][0] != socket.AF_INET6:
                self.skipTest("IPv6 loopback '::1' did not resolve to AF_INET6")
        except OSError:
            self.skipTest("IPv6 loopback '::1' not available")

        mock_sock = mock_socket_cls.return_value.__enter__.return_value
        mock_sock.bind.return_value = None

        port = find_available_port(start_port=8085, host="::1")
        self.assertEqual(port, 8085)
        mock_socket_cls.assert_called_with(socket.AF_INET6, socket.SOCK_STREAM)

    @patch("asyncio.create_subprocess_exec")
    async def test_launch_aquarium_cmd_includes_http_port_and_address(
        self, mock_subprocess
    ):
        mock_proc = AsyncMock()
        mock_proc.stdout = AsyncMock()
        mock_proc.stdout.readline = AsyncMock(return_value=b"")
        mock_proc.returncode = None
        mock_subprocess.return_value = mock_proc

        runner = EmulatorRunner(
            command=["emulator"],
            aquarium_exec=Path("/tmp/aquarium-ui"),
            env={},
            http_port=9090,
            http_address="127.0.0.1",
        )

        with patch("emulator_lib.find_available_port", return_value=9095):
            await runner._launch_aquarium("/tmp/discovery.ini")

        mock_subprocess.assert_called_once()
        cmd_args = list(mock_subprocess.call_args[0])
        self.assertIn("--discovery_file", cmd_args)
        self.assertIn("/tmp/discovery.ini", cmd_args)
        self.assertIn("--http_address", cmd_args)
        self.assertIn("127.0.0.1", cmd_args)
        self.assertIn("--http_port", cmd_args)
        self.assertIn("9095", cmd_args)

    async def test_stream_output_invokes_on_emulator_line(self):
        runner = EmulatorRunner(
            command=["emulator"],
            aquarium_exec=Path("/tmp/aquarium-ui"),
            env={},
        )
        lines = [b"booting...\n", b"ready.\n", b""]
        mock_stream = AsyncMock()
        mock_stream.readline.side_effect = lines

        handled_lines = []

        async def fake_on_line(line):
            handled_lines.append(line)

        runner._on_emulator_line = fake_on_line
        await runner._stream_output(mock_stream)

        self.assertEqual(handled_lines, ["booting...", "ready."])

    @patch("asyncio.create_subprocess_exec")
    async def test_stream_output_launches_aquarium_when_target_log_falsy(
        self, mock_subprocess
    ):
        mock_proc = AsyncMock()
        mock_proc.stdout = AsyncMock()
        mock_proc.stdout.readline = AsyncMock(return_value=b"")
        mock_proc.returncode = None
        mock_subprocess.return_value = mock_proc

        runner = EmulatorRunner(
            command=["emulator"],
            aquarium_exec=Path("/tmp/aquarium-ui"),
            env={},
        )
        lines = [
            b"booting...\n",
            b"Advertising in discovery file: /tmp/discovery.ini\n",
            b"ready.\n",
            b"",
        ]
        mock_stream = AsyncMock()
        mock_stream.readline.side_effect = lines

        with patch("emulator_lib.find_available_port", return_value=8085):
            status = await runner._stream_output_and_find_log(
                mock_stream, target_log=None
            )

        self.assertTrue(status)
        self.assertIsNotNone(runner.aquarium)
        mock_subprocess.assert_called_once()
        cmd_args = list(mock_subprocess.call_args[0])
        self.assertIn("/tmp/discovery.ini", cmd_args)


if __name__ == "__main__":
    unittest.main()
