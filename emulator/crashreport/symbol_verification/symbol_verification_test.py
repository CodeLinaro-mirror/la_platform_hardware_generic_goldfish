import os
import sys
import struct
import unittest
from python.runfiles import runfiles
from pathlib import Path
import zipfile
import subprocess


class SymbolVerificationTest(unittest.TestCase):
    """Test suite validating Breakpad symbol files and binary stripping.

    This class verifies that:
    - The Breakpad symbol generation output package has valid MODULE and FILE records.
    - Expected function symbols (e.g. main, functionA, functionB, functionC) are
      present in the symbol file.
    - Debug line numbers mapping to the C++ source file are preserved in the symbols.
    - Stripped binaries are verified to be correctly post-processed, verifying PE PDB
      reference stripping on Windows, Mach-O symbol table entry reduction on macOS, and
      ELF SHT_SYMTAB section removal on Linux.
    """

    _symbols_printed = False

    @classmethod
    def setUpClass(cls):
        r = runfiles.Create()
        cls.zip_path = cls._locate_symbols_zip()
        cls.sym_content = cls._extract_symbol_file(cls.zip_path)

    @classmethod
    def _locate_symbols_zip(cls) -> Path:
        runfiles_base = (
            os.environ.get("RUNFILES_DIR") or os.environ.get("PYTHON_RUNFILES") or "."
        )
        runfiles_dir = Path(runfiles_base)
        print(
            f"Searching for symbol_verification-symbols.zip starting from runfiles_dir: {runfiles_dir}"
        )

        for p in runfiles_dir.rglob("symbol_verification-symbols.zip"):
            print(f"Found symbol_verification-symbols.zip at: {p}")
            return p

        print("All files found in runfiles:")
        for p in list(runfiles_dir.rglob("*"))[:100]:
            print(p)

        raise FileNotFoundError(
            f"Could not find symbol_verification-symbols.zip in the runfiles directory tree. Runfiles dir: {runfiles_dir}"
        )

    @classmethod
    def _extract_symbol_file(cls, zip_path: Path) -> str:
        with zipfile.ZipFile(zip_path, "r") as zf:
            for name in zf.namelist():
                if name.endswith(".sym"):
                    print(f"Reading symbol file from zip: {name}")
                    return zf.read(name).decode("utf-8")
        raise ValueError(f"Could not find any .sym file inside {zip_path.name}")

    def test_basic_structure(self):
        lines = self.sym_content.splitlines()
        first_line = lines[0].lower()
        if not (
            first_line.startswith("module mac ")
            or first_line.startswith("module linux ")
            or first_line.startswith("module windows ")
        ):
            self.fail_with_symbols(
                f"First line must be a MODULE record, got: {lines[0].strip()}"
            )

    def test_source_file_references(self):
        filename = "symbol_verification.cc"
        has_file_record = any(
            line.startswith("FILE ") and filename in line
            for line in self.sym_content.splitlines()
        )
        if not has_file_record:
            self.fail_with_symbols(f"Could not find FILE record for {filename}")

    def test_line_numbers_present(self):
        # 1. Find the FILE index for symbol_verification.cc
        file_idx = None
        for line in self.sym_content.splitlines():
            if line.startswith("FILE "):
                parts = line.split()
                if len(parts) >= 3 and "symbol_verification.cc" in parts[2]:
                    file_idx = parts[1]
                    break

        if file_idx is None:
            self.fail_with_symbols(
                "Could not determine FILE index for symbol_verification.cc"
            )

        # 2. Assert there are line records referencing this FILE index
        # Format: <address> <size> <line_number> <file_index>
        has_line_numbers = False
        for line in self.sym_content.splitlines():
            parts = line.split()
            if len(parts) == 4:
                if parts[2].isdigit() and parts[3] == file_idx:
                    has_line_numbers = True
                    break

        if not has_line_numbers:
            self.fail_with_symbols(
                f"Could not find any line number mapping records for symbol_verification.cc (FILE index {file_idx})"
            )

    def test_function_symbol_main_present(self):
        self.assert_function_symbol_present("main")

    def test_function_symbol_function_a_present(self):
        self.assert_function_symbol_present("functionA")

    def test_function_symbol_function_b_present(self):
        self.assert_function_symbol_present("functionB")

    def test_function_symbol_function_c_present(self):
        self.assert_function_symbol_present("functionC")

    def assert_function_symbol_present(self, func_name: str):
        found = any(
            (line.startswith("FUNC ") or line.startswith("INLINE_ORIGIN "))
            and func_name in line
            for line in self.sym_content.splitlines()
        )
        if not found:
            self.fail_with_symbols(
                f"Could not find FUNC/INLINE_ORIGIN record for '{func_name}'"
            )

    def test_stripping_works(self):
        r = runfiles.Create()
        binary_name = (
            "symbol_verification.exe"
            if sys.platform == "win32"
            else "symbol_verification"
        )

        unstripped_path, stripped_path = self._locate_binaries(r, binary_name)

        self.assertLess(
            stripped_path.stat().st_size,
            unstripped_path.stat().st_size,
            "Stripped binary should be smaller than unstripped binary",
        )

        if sys.platform == "win32":
            self._verify_win32_stripping(unstripped_path, stripped_path)
        elif sys.platform == "darwin":
            self._verify_darwin_stripping(unstripped_path, stripped_path)
        else:
            self._verify_linux_stripping(unstripped_path, stripped_path)

    def _locate_binaries(self, r, binary_name: str) -> tuple[Path, Path]:
        runfiles_base = (
            os.environ.get("RUNFILES_DIR") or os.environ.get("PYTHON_RUNFILES") or "."
        )
        runfiles_dir = Path(runfiles_base)

        unstripped_path = None
        stripped_path = None

        for p in runfiles_dir.rglob(binary_name):
            if (
                p.is_file()
                and not p.name.endswith(".py")
                and not p.name.endswith(".sh")
            ):
                if "_stripped" in p.parts:
                    stripped_path = p
                elif "symbol_verification.runfiles" not in p.parts:
                    unstripped_path = p

        self.assertIsNotNone(
            unstripped_path, f"Could not locate unstripped {binary_name} binary"
        )
        self.assertIsNotNone(
            stripped_path, f"Could not locate stripped {binary_name} binary"
        )
        print(
            f"Unstripped path: {unstripped_path} (size: {unstripped_path.stat().st_size})"
        )
        print(f"Stripped path: {stripped_path} (size: {stripped_path.stat().st_size})")
        return unstripped_path, stripped_path

    def _verify_win32_stripping(self, unstripped_path: Path, stripped_path: Path):
        """Verifies that the Windows PE binary has been stripped of its PDB debug path.

        Windows compilers embed the absolute path of the generated .pdb file as a
        null-terminated string inside the PE binary's CODEVIEW debug directory.
        Stripping removes this debug directory, so we assert that the '.pdb'
        extension string is present in the unstripped binary and absent in the stripped one.

        Note: This simple search for the b".pdb" substring is sufficient for our test case
        as the symbol_verification binary does not contain this substring anywhere else in
        its code or static data.

        Args:
            unstripped_path: Path to the unstripped PE binary.
            stripped_path: Path to the stripped PE binary.
        """
        with open(unstripped_path, "rb") as f:
            unstripped_data = f.read()
        with open(stripped_path, "rb") as f:
            stripped_data = f.read()

        self.assertIn(
            b".pdb",
            unstripped_data,
            "Unstripped Windows binary should contain a PDB reference",
        )
        self.assertNotIn(
            b".pdb",
            stripped_data,
            "Stripped Windows binary should not contain a PDB reference",
        )

    def _verify_darwin_stripping(self, unstripped_path: Path, stripped_path: Path):
        # On macOS, verify that the number of symbols in the Mach-O symbol table (LC_SYMTAB) is reduced
        unstripped_nsyms = self._get_macho_nsyms(unstripped_path)
        stripped_nsyms = self._get_macho_nsyms(stripped_path)
        print(f"Unstripped Mach-O symbols: {unstripped_nsyms}")
        print(f"Stripped Mach-O symbols: {stripped_nsyms}")

        self.assertGreater(
            unstripped_nsyms, 0, "Unstripped Mach-O should contain symbol table entries"
        )
        self.assertGreater(
            unstripped_nsyms,
            stripped_nsyms,
            "Stripped Mach-O should have fewer symbols than unstripped Mach-O",
        )

    def _verify_linux_stripping(self, unstripped_path: Path, stripped_path: Path):
        # On Linux ELF, verify that SHT_SYMTAB (.symtab) is present in the unstripped binary and absent in the stripped one
        self.assertTrue(
            self._has_elf_symtab(unstripped_path),
            "Unstripped ELF binary should contain a .symtab section",
        )
        self.assertFalse(
            self._has_elf_symtab(stripped_path),
            "Stripped ELF binary should not contain a .symtab section",
        )

    def _get_macho_nsyms(self, path: Path) -> int:
        """Parses a 64-bit little-endian Mach-O binary and extracts nsyms from LC_SYMTAB.

        This method reads the Mach-O file header and scans through its load commands
        to find the symbol table command (LC_SYMTAB, cmd=2). It unpacks the symbol
        table size (nsyms) to determine the number of entries in the symbol table,
        which allows verifying whether local and debug symbols have been stripped.

        Args:
            path: Path to the Mach-O binary file.

        Returns:
            The number of symbols (nsyms) defined in the LC_SYMTAB load command,
            or 0 if the file is invalid or not a 64-bit little-endian Mach-O binary.
        """
        with open(path, "rb") as f:
            header = f.read(32)
            if len(header) < 32:
                return 0
            magic = struct.unpack("<I", header[0:4])[0]
            if magic != 0xFEEDFACF:
                return 0
            ncmds = struct.unpack("<I", header[16:20])[0]

            offset = 32
            for _ in range(ncmds):
                f.seek(offset)
                cmd_data = f.read(8)
                if len(cmd_data) < 8:
                    break
                cmd, cmdsize = struct.unpack("<II", cmd_data)
                if cmd == 2:  # LC_SYMTAB
                    sym_data = f.read(8)
                    if len(sym_data) < 8:
                        break
                    _, nsyms = struct.unpack("<II", sym_data)
                    return nsyms
                offset += cmdsize
        return 0

    def _has_elf_symtab(self, path: Path) -> bool:
        """Parses a 64-bit little-endian ELF binary to check for the presence of the .symtab section.

        This parses the ELF file identification header and segment metadata natively.
        It reads the Section Header Table offset (shoff) and reads all section headers.
        If it finds a section header with type SHT_SYMTAB (value 2), it returns True,
        meaning the binary contains a non-stripped symbol table.

        Args:
            path: Path to the ELF binary file.

        Returns:
            True if the SHT_SYMTAB (value 2) section header exists, False otherwise (fully stripped).
        """
        with open(path, "rb") as f:
            ident = f.read(16)
            if len(ident) < 16 or ident[0:4] != b"\x7fELF":
                return False
            if ident[4] != 2 or ident[5] != 1:
                return False

            f.seek(40)
            shoff = struct.unpack("<Q", f.read(8))[0]
            f.seek(58)
            shentsize = struct.unpack("<H", f.read(2))[0]
            shnum = struct.unpack("<H", f.read(2))[0]

            if shoff == 0 or shnum == 0:
                return False

            for i in range(shnum):
                f.seek(shoff + i * shentsize + 4)
                sh_type = struct.unpack("<I", f.read(4))[0]
                if sh_type == 2:  # SHT_SYMTAB
                    return True
        return False

    def fail_with_symbols(self, msg: str):
        if not SymbolVerificationTest._symbols_printed:
            print("\n--- COMPLETE SYMBOL FILE CONTENT START ---")
            print(self.sym_content)
            print("--- COMPLETE SYMBOL FILE CONTENT END ---\n")
            SymbolVerificationTest._symbols_printed = True
        self.fail(msg)


if __name__ == "__main__":
    unittest.main()
