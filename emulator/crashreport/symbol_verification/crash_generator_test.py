import os
import sys
import unittest
import shutil
import tempfile
import subprocess
import zipfile
from pathlib import Path
from python.runfiles import runfiles
import time


class CrashGeneratorTest(unittest.TestCase):
    """Integration test checking that crash minidump line numbers are correctly resolved.

    This test executes a standalone C++ crash generator binary ('crash_generator')
    configured to crash with a Crashpad handler and database. It waits for the
    minidump (.dmp) file to be written, extracts the built Breakpad symbols,
    and runs the host-compiled Breakpad 'minidump_stackwalk' utility.

    Finally, it asserts that the symbolicated stack trace resolves line numbers
    in crash_generator.cc correctly, validating that our Breakpad debug symbols
    generation is correct and fully matches the binary's actual instructions.
    """

    def test_minidump_resolves_line_numbers(self):
        r = runfiles.Create()

        # Locate binaries and assets
        crash_gen_path, handler_path, symbols_zip_path, stackwalk_path = (
            self.locate_dependencies(r)
        )

        # Create temp dir and crash database dir
        with tempfile.TemporaryDirectory() as temp_dir_str:
            temp_dir = Path(temp_dir_str)
            db_dir = temp_dir / "crash_db"
            db_dir.mkdir()

            # Run crash generator (expect crash exit code != 0)
            self.run_crash_generator(crash_gen_path, handler_path, db_dir)

            # Locate the generated minidump (.dmp) file
            dmp_file_path = self.wait_for_minidump(db_dir)

            # Extract symbols
            symbols_dir = temp_dir / "symbols"
            self.extract_symbols(symbols_zip_path, symbols_dir)

            # Run minidump_stackwalk
            stack_output = self.run_stackwalk(
                stackwalk_path, dmp_file_path, symbols_dir
            )

            # Verify the stack trace has correct functions and line numbers
            self.verify_stack_trace_resolved_lines(stack_output)
            print("Minidump line number validation succeeded!")

    def locate_dependencies(self, r) -> tuple[Path, Path, Path, Path]:
        crash_gen_path = self.locate_file(r, "crash_generator")
        handler_path = self.locate_file(r, "crashpad_handler")
        symbols_zip_path = self.locate_file(r, "crash_generator-symbols.zip")
        stackwalk_path = self.locate_file(r, "minidump_stackwalk")
        return crash_gen_path, handler_path, symbols_zip_path, stackwalk_path

    def run_crash_generator(
        self, crash_gen_path: Path, handler_path: Path, db_dir: Path
    ):
        print(f"Running crash_generator to generate minidump in db: {db_dir}")
        proc = subprocess.run(
            [str(crash_gen_path), str(handler_path), str(db_dir)],
            capture_output=True,
            text=True,
        )
        print("crash_generator stdout:")
        print(proc.stdout)
        print("crash_generator stderr:")
        print(proc.stderr)

    def wait_for_minidump(self, db_dir: Path, timeout: float = 30.0) -> Path:
        print(
            f"Waiting for minidump (.dmp) file to be written (timeout: {timeout}s)..."
        )
        start_time = time.time()
        while time.time() < start_time + timeout:
            dmp_files = list(db_dir.rglob("*.dmp"))
            if dmp_files:
                dmp_file_path = dmp_files[0]
                print(f"Generated minidump file: {dmp_file_path}")
                return dmp_file_path
            time.sleep(0.1)
        self.fail(f"No minidump (.dmp) file was generated after {timeout} seconds!")

    def extract_symbols(self, symbols_zip_path: Path, symbols_dir: Path):
        symbols_dir.mkdir()
        print(f"Extracting symbols to: {symbols_dir}")
        with zipfile.ZipFile(symbols_zip_path, "r") as zf:
            zf.extractall(symbols_dir)

        print("Extracted symbol files:")
        for p in symbols_dir.rglob("*"):
            if p.is_file():
                print(p)

    def run_stackwalk(
        self, stackwalk_path: Path, dmp_file_path: Path, symbols_dir: Path
    ) -> str:
        print("Running minidump_stackwalk...")
        sw_proc = subprocess.run(
            [str(stackwalk_path), str(dmp_file_path), str(symbols_dir)],
            capture_output=True,
            text=True,
        )
        print("--- minidump_stackwalk stdout ---")
        print(sw_proc.stdout)
        print("--- minidump_stackwalk stderr ---")
        print(sw_proc.stderr)

        self.assertEqual(
            sw_proc.returncode,
            0,
            f"minidump_stackwalk failed with code {sw_proc.returncode}",
        )
        return sw_proc.stdout

    def verify_stack_trace_resolved_lines(self, raw_stack_output: str):
        stack_output = raw_stack_output.replace(" ", "")
        self.assertIn("crash_generator.cc:15", stack_output)
        self.assertIn("crash_generator.cc:20", stack_output)
        self.assertIn("crash_generator.cc:25", stack_output)
        self.assertIn("crash_generator.cc:66", stack_output)
      
    def locate_file(self, r, filename: str) -> Path:
        runfiles_base = (
            os.environ.get("RUNFILES_DIR") or os.environ.get("PYTHON_RUNFILES") or "."
        )
        runfiles_dir = Path(runfiles_base)

        # Try finding via runfiles library first
        r_path = r.Rlocation(
            f"goldfish/emulator/crashreport/symbol_verification/{filename}"
        )
        if r_path and os.path.exists(r_path):
            return Path(r_path)

        r_path = r.Rlocation(f"crashpad/handler/{filename}")
        if r_path and os.path.exists(r_path):
            return Path(r_path)

        r_path = r.Rlocation(f"goldfish_prebuilts_common/{filename}")
        if r_path and os.path.exists(r_path):
            return Path(r_path)

        # Fallback recursive glob
        for p in runfiles_dir.rglob(filename):
            return p
        for p in runfiles_dir.rglob(filename + ".exe"):
            return p

        # Dump runfiles
        print(f"Could not locate {filename} under runfiles. Listing files:")
        for p in list(runfiles_dir.rglob("*"))[:50]:
            print(p)
        self.fail(f"Could not find dependency file: {filename}")


if __name__ == "__main__":
    unittest.main()
