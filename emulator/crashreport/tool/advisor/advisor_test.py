# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Unit tests for CrashAdvisor (advisor) pipeline classes."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from api import CrashApi
from client import GossoClient
from context import CrashReportContext
from dump import CrashReportDumper
from metadata import CrashMetadata
from rca import CrashReportAnalyzer
from symbols import SymbolFetcher


class TestCrashAdvisor(unittest.TestCase):
    """Test suite covering all modular classes of the CrashAdvisor pipeline."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.temp_path = Path(self.temp_dir.name)
        self.crash_id = "6998451fea502b78"

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    @patch("client.shutil.which")
    def test_gosso_client_init(self, mock_which: MagicMock) -> None:
        """Test GossoClient initialization and binary path resolution."""
        mock_which.return_value = "/usr/bin/gosso"
        client = GossoClient()
        self.assertEqual(client.gosso_path, "/usr/bin/gosso")

    @patch("client.shutil.which")
    def test_gosso_client_init_raises_runtime_error(
        self, mock_which: MagicMock
    ) -> None:
        """Test GossoClient raises RuntimeError when gosso binary is missing."""
        mock_which.return_value = None
        with self.assertRaises(RuntimeError):
            GossoClient()

    @patch("client.subprocess.run")
    def test_gosso_fetch_url(self, mock_run: MagicMock) -> None:
        """Test GossoClient fetch_url_to_file execution."""
        client = GossoClient(gosso_path="/usr/bin/gosso")
        out_path = self.temp_path / "out.dmp"

        def side_effect(*args, **kwargs):
            out_path.write_text("dummy content")
            return MagicMock(returncode=0)

        mock_run.side_effect = side_effect
        client.fetch_url_to_file("http://fake.url", out_path)
        self.assertTrue(out_path.exists())
        mock_run.assert_called_once()

    def test_context_sandbox_preparation(self) -> None:
        """Test CrashReportContext crash ID parsing and sandbox directory creation."""
        context = CrashReportContext(
            f"http://go/crash/{self.crash_id}", base_dir=str(self.temp_path), aosp_root="/qemu2/root", branch="emu-main-dev", build_id="12345678"
        )
        self.assertEqual(context.crash_id, self.crash_id)
        self.assertEqual(context.aosp_root, "/qemu2/root")
        self.assertEqual(context.branch, "emu-main-dev")
        self.assertEqual(context.build_id, "12345678")
        context.prepare_sandbox()
        self.assertTrue(context.work_dir.exists())
        self.assertEqual(context.metadata_path, context.work_dir / "metadata.json")
        self.assertEqual(context.minidump_path, context.work_dir / "minidump.dmp")

    def test_crash_api_download_metadata_and_minidump(self) -> None:
        """Test CrashApi download methods and in-place pretty printing."""
        mock_gosso = MagicMock()
        api = CrashApi(mock_gosso, verbose=True)

        meta_path = self.temp_path / "metadata.json"
        dmp_path = self.temp_path / "minidump.dmp"

        # Mock fetch_url_to_file writing a raw JSON string
        def mock_fetch(url, out_path):
            if "get_report" in url:
                out_path.write_text(
                    '{"report_proto": {"ReportID": "6998451fea502b78"}}'
                )
            else:
                out_path.write_text("MDMP...")

        mock_gosso.fetch_url_to_file.side_effect = mock_fetch

        api.download_metadata(self.crash_id, meta_path)
        api.download_minidump(self.crash_id, dmp_path)

        self.assertTrue(meta_path.exists())
        self.assertTrue(dmp_path.exists())

        # Verify pretty-printing format check
        content = meta_path.read_text()
        self.assertIn('\n  "report_proto"', content)

    def test_crash_api_download_metadata_no_verbose(self) -> None:
        """Test CrashApi download_metadata does not pretty print when verbose is False."""
        mock_gosso = MagicMock()
        api = CrashApi(mock_gosso, verbose=False)

        meta_path = self.temp_path / "metadata.json"
        raw_json = '{"report_proto": {"ReportID": "6998451fea502b78"}}'

        def mock_fetch(url, out_path):
            if "get_report" in url:
                out_path.write_text(raw_json)

        mock_gosso.fetch_url_to_file.side_effect = mock_fetch

        api.download_metadata(self.crash_id, meta_path)
        self.assertTrue(meta_path.exists())

        # Verify content remains raw (no pretty printing)
        content = meta_path.read_text()
        self.assertEqual(content, raw_json)

    def test_crash_metadata_parsing(self) -> None:
        """Test CrashMetadata structural decoding and build_target mapping."""
        meta_path = self.temp_path / "metadata.json"
        mock_data = {
            "report_proto": {
                "ReportID": self.crash_id,
                "product": {"Name": "AndroidEmulator", "Version": "0.0.1-15630821"},
                "os": {"Name": "Linux", "Version": "Ubuntu"},
                "cpu": {"Architecture": "x86_64"},
                "stableSignature": "CrashReporterImpl::Die",
                "productdata": [{"Key": "internal-msg", "Value": "hanging thread"}],
                "Module": [{"DebugFile": "qemu", "DebugIdentifier": "1234"}],
            }
        }
        meta_path.write_text(json.dumps(mock_data))

        metadata = CrashMetadata(meta_path)
        self.assertEqual(metadata.report_id, self.crash_id)
        self.assertEqual(metadata.product_name, "AndroidEmulator")
        self.assertEqual(metadata.build_id, "15630821")
        self.assertEqual(metadata.os_summary, "Linux (Ubuntu)")
        self.assertEqual(metadata.build_target, "emulator_linux_x64")
        self.assertEqual(metadata.custom_keys, {"internal-msg": "hanging thread"})
        self.assertEqual(metadata.modules, [("qemu", "1234")])

    def test_crash_metadata_explicit_build_id_override(self) -> None:
        """Test CrashMetadata prioritizes explicit build ID override over product version."""
        meta_path = self.temp_path / "metadata_override.json"
        meta_path.write_text(json.dumps({"report_proto": {"product": {"Version": "36.3.10"}}}))
        
        # Without override
        metadata_normal = CrashMetadata(meta_path)
        self.assertEqual(metadata_normal.build_id, "36.3.10")

        # With override
        metadata_override = CrashMetadata(meta_path, build_id="15630821")
        self.assertEqual(metadata_override.build_id, "15630821")

    def test_crash_metadata_emu_main_dev_gfxstream(self) -> None:
        """Test CrashMetadata maps OS/CPU to gfxstream targets for emu-main-dev branch."""
        meta_path = self.temp_path / "metadata_dev.json"

        # Test Linux x64 gfxstream
        meta_path.write_text(json.dumps({"report_proto": {"os": {"Name": "Linux"}, "cpu": {"Architecture": "x86_64"}}}))
        metadata_linux = CrashMetadata(meta_path, branch="emu-main-dev")
        self.assertEqual(metadata_linux.build_target, "emulator-linux_x64_gfxstream")

        # Test macOS aarch64 gfxstream
        meta_path.write_text(json.dumps({"report_proto": {"os": {"Name": "Darwin"}, "cpu": {"Architecture": "aarch64"}}}))
        metadata_mac = CrashMetadata(meta_path, branch="emu-main-dev")
        self.assertEqual(metadata_mac.build_target, "emulator-mac_aarch64_gfxstream")

        # Test Windows x64 gfxstream
        meta_path.write_text(json.dumps({"report_proto": {"os": {"Name": "Windows"}, "cpu": {"Architecture": "x86_64"}}}))
        metadata_win = CrashMetadata(meta_path, branch="emu-main-dev")
        self.assertEqual(metadata_win.build_target, "emulator-windows_x64_gfxstream")

    @patch("symbols.AndroidBuildClient")
    def test_symbol_fetcher(self, mock_ab_class: MagicMock) -> None:
        """Test SymbolFetcher pipeline and global caching."""
        mock_api = MagicMock()
        mock_ab_client = MagicMock()
        mock_ab_client.list_artifacts.return_value = ["sdk-repo-linux-emulator-breakpad-symbols-15630821.zip"]
        mock_ab_class.return_value = mock_ab_client

        cache_dir = self.temp_path / "cache"
        symbols_dir = self.temp_path / "symbols"

        meta_path = self.temp_path / "metadata.json"
        meta_path.write_text(
            json.dumps(
                {
                    "report_proto": {
                        "product": {"Version": "15630821"},
                        "os": {"Name": "Linux"},
                    }
                }
            )
        )
        metadata = CrashMetadata(meta_path)

        fetcher = SymbolFetcher(mock_api, global_cache_dir=str(cache_dir))

        # Mock fetch_bits creating a dummy zip file and unpack_archive mocking
        with patch("symbols.shutil.unpack_archive") as mock_unpack:

            def mock_fetch_bits(dst, bid, target, artifact):
                Path(dst).write_text("PK...")

            mock_ab_client.fetch_bits.side_effect = mock_fetch_bits

            fetcher.fetch_symbols(metadata, symbols_dir, custom_token="dummy_token")
            mock_unpack.assert_called_once()

            # Verify global cache location
            cached_zip = (
                cache_dir
                / "15630821"
                / "sdk-repo-linux-emulator-breakpad-symbols-15630821.zip"
            )
            self.assertTrue(cached_zip.exists())

    @patch("symbols.AndroidBuildClient")
    def test_symbol_fetcher_emulator_symbols_dynamic_resolution(self, mock_ab_class: MagicMock) -> None:
        """Test SymbolFetcher dynamically resolves emulator-symbols zip for gfxstream targets."""
        mock_api = MagicMock()
        mock_ab_client = MagicMock()
        mock_ab_client.list_artifacts.return_value = ["sdk-repo-linux-emulator-symbols-15659437.zip"]
        mock_ab_class.return_value = mock_ab_client

        cache_dir = self.temp_path / "cache_dyn"
        symbols_dir = self.temp_path / "symbols_dyn"

        meta_path = self.temp_path / "metadata_dyn.json"
        meta_path.write_text(json.dumps({"report_proto": {"product": {"Version": "15659437"}, "os": {"Name": "Linux"}}}))
        metadata = CrashMetadata(meta_path, branch="emu-main-dev")

        fetcher = SymbolFetcher(mock_api, global_cache_dir=str(cache_dir))

        with patch("symbols.shutil.unpack_archive") as mock_unpack:
            def mock_fetch_bits(dst, bid, target, artifact):
                Path(dst).write_text("PK...")

            mock_ab_client.fetch_bits.side_effect = mock_fetch_bits

            fetcher.fetch_symbols(metadata, symbols_dir, custom_token="dummy_token")
            mock_unpack.assert_called_once()

            cached_zip = cache_dir / "15659437" / "sdk-repo-linux-emulator-symbols-15659437.zip"
            self.assertTrue(cached_zip.exists())

    @patch("dump.Runfiles")
    @patch("dump.subprocess.run")
    def test_crash_report_dumper(
        self, mock_run: MagicMock, mock_runfiles: MagicMock
    ) -> None:
        """Test CrashReportDumper executing C++ binary for text and JSON dumps."""
        mock_rf_inst = MagicMock()
        mock_rf_inst.Rlocation.return_value = (
            sys.executable  # Use existing binary as dummy path
        )
        mock_runfiles.Create.return_value = mock_rf_inst

        mock_run.return_value = MagicMock(returncode=0, stderr="")

        context = CrashReportContext(self.crash_id, base_dir=str(self.temp_path))
        context.prepare_sandbox()
        symbols_dir = self.temp_path / "symbols"

        dumper = CrashReportDumper()
        dump_path = dumper.generate_dump(context, symbols_dir)

        self.assertEqual(dump_path, context.work_dir / "crashreport.txt")
        self.assertEqual(mock_run.call_count, 2)  # Called for txt and json

    def test_crash_report_analyzer(self) -> None:
        """Test CrashReportAnalyzer generating investigation_cmd.sh interactive script."""
        context = CrashReportContext(self.crash_id, base_dir=str(self.temp_path))
        context.prepare_sandbox()
        dump_path = context.work_dir / "crashreport.txt"
        dump_path.write_text("Crashing stack trace dummy")

        analyzer = CrashReportAnalyzer()
        script_path = analyzer.generate_explanation(context, dump_path)

        self.assertTrue(script_path.exists())
        self.assertEqual(script_path, context.work_dir / "investigation_cmd.sh")
        content = script_path.read_text()
        self.assertIn("jetski", content)
        self.assertIn("--prompt-interactive", content)
        self.assertIn(f"--add-dir={context.work_dir}", content)

    def test_crash_report_analyzer_custom_aosp_root(self) -> None:
        """Test CrashReportAnalyzer respects custom aosp_root for qemu2 branch support."""
        context = CrashReportContext(
            self.crash_id, base_dir=str(self.temp_path), aosp_root="/qemu2/custom/root"
        )
        context.prepare_sandbox()
        dump_path = context.work_dir / "crashreport.txt"
        dump_path.write_text("Crashing stack trace dummy")

        analyzer = CrashReportAnalyzer()
        script_path = analyzer.generate_explanation(context, dump_path)

        self.assertTrue(script_path.exists())
        content = script_path.read_text()
        self.assertIn("--add-dir=/qemu2/custom/root", content)

    def test_crash_report_analyzer_raises_file_not_found(self) -> None:
        """Test CrashReportAnalyzer raises FileNotFoundError when dump file is missing."""
        context = CrashReportContext(self.crash_id, base_dir=str(self.temp_path))
        context.prepare_sandbox()
        dump_path = context.work_dir / "nonexistent.txt"

        analyzer = CrashReportAnalyzer()
        with self.assertRaises(FileNotFoundError):
            analyzer.generate_explanation(context, dump_path)


if __name__ == "__main__":
    unittest.main()
