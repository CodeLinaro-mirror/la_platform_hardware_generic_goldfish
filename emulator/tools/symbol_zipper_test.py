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

import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from emulator.tools.symbol_zipper import (
    ZIP_EPOCH,
    extract_module_info,
    parse_breakpad_line,
    parse_date,
    symbol_destination,
)


class SymbolZipperTest(unittest.TestCase):
    def test_parse_breakpad_line(self):
        line = "MODULE Linux x86_64 A1B2C3D4E5F678901234567890ABCDEF0 mymodule.so\n"
        mod = parse_breakpad_line(line)
        self.assertIsNotNone(mod)
        self.assertEqual(mod.operatingsystem, "Linux")
        self.assertEqual(mod.architecture, "x86_64")
        self.assertEqual(mod.id, "A1B2C3D4E5F678901234567890ABCDEF0")
        self.assertEqual(mod.name, "mymodule.so")

    def test_parse_non_module_line(self):
        line = "FILE 1 /path/to/source.cc\n"
        mod = parse_breakpad_line(line)
        self.assertIsNone(mod)

    def test_symbol_destination(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            sym_file = Path(tmpdir) / "test.sym"
            sym_file.write_text(
                "MODULE Darwin arm64 4C4C444655553144A1925AF7C65861290 libtest.dylib\n"
                "PUBLIC 1000 0 func\n",
                encoding="utf-8",
            )
            dest = symbol_destination(sym_file)
            self.assertEqual(
                str(dest).replace("\\", "/"),
                "libtest.dylib/4C4C444655553144A1925AF7C65861290/libtest.dylib.sym",
            )

    def test_zip_with_compression_levels(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            sym_file = tmp_path / "sample.sym"
            sym_content = (
                "MODULE Linux x86_64 11223344556677889900AABBCCDDEEFF0 libsample.so\n"
                + "PUBLIC 100 0 some_function\n" * 200
            )
            sym_file.write_text(sym_content, encoding="utf-8")

            zipper_py = Path(__file__).parent / "symbol_zipper.py"

            # Test using default / 7za
            zip_lvl0 = tmp_path / "symbols_lvl0.zip"
            subprocess.check_call(
                [
                    sys.executable,
                    str(zipper_py),
                    "-c",
                    "0",
                    "-o",
                    str(zip_lvl0),
                    str(sym_file),
                ]
            )
            with zipfile.ZipFile(zip_lvl0, "r") as zf:
                info = zf.getinfo("libsample.so/11223344556677889900AABBCCDDEEFF0/libsample.so.sym")
                self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(zf.read(info.filename).decode("utf-8"), sym_content)

            zip_lvl1 = tmp_path / "symbols_lvl1.zip"
            subprocess.check_call(
                [
                    sys.executable,
                    str(zipper_py),
                    "-c",
                    "1",
                    "-o",
                    str(zip_lvl1),
                    str(sym_file),
                ]
            )
            with zipfile.ZipFile(zip_lvl1, "r") as zf:
                info = zf.getinfo("libsample.so/11223344556677889900AABBCCDDEEFF0/libsample.so.sym")
                self.assertEqual(info.compress_type, zipfile.ZIP_DEFLATED)
                self.assertLess(info.compress_size, info.file_size)
                self.assertEqual(zf.read(info.filename).decode("utf-8"), sym_content)

    def test_response_file_and_sevenzip(self):
        import shutil
        sevenzip_exe = shutil.which("7za")
        if not sevenzip_exe:
            self.skipTest("7za not available in PATH")

        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            sym_file = tmp_path / "mod.sym"
            sym_content = "MODULE Linux x86_64 ABCDEF1234567890ABCDEF12345678900 libmod.so\nPUBLIC 0 0 fn\n"
            sym_file.write_text(sym_content, encoding="utf-8")

            zipper_py = Path(__file__).parent / "symbol_zipper.py"
            out_zip = tmp_path / "response_test.zip"
            param_file = tmp_path / "params.txt"
            param_file.write_text(
                f"-o\n{out_zip}\n-c\n1\n--sevenzip\n{sevenzip_exe}\n{sym_file}\n",
                encoding="utf-8",
            )

            subprocess.check_call(
                [
                    sys.executable,
                    str(zipper_py),
                    f"@{param_file}",
                ]
            )
            with zipfile.ZipFile(out_zip, "r") as zf:
                info = zf.getinfo("libmod.so/ABCDEF1234567890ABCDEF12345678900/libmod.so.sym")
                self.assertEqual(info.compress_type, zipfile.ZIP_DEFLATED)
                self.assertEqual(zf.read(info.filename).decode("utf-8"), sym_content)

    def test_zip_epoch_utc(self):
        self.assertEqual(int(ZIP_EPOCH), 315532800)
        self.assertEqual(parse_date(ZIP_EPOCH), (1980, 1, 1, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()
