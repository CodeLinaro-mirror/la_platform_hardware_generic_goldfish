import argparse
import stat
import sys
import unittest
import zipfile

from python.runfiles import Runfiles

ARGS = argparse.Namespace()

KNOWN_EXECUTABLES = {
    "emulator/emulator",
    "emulator/emulator-check",
    "emulator/mksdcard",
}


def find_symlinks(zf: zipfile.ZipFile) -> list[tuple[str, str]]:
    """Returns a list of (filename, link_target) for all symbolic link entries in the archive."""
    symlinks: list[tuple[str, str]] = []
    for info in zf.infolist():
        mode = info.external_attr >> 16
        if stat.S_ISLNK(mode):
            try:
                target = zf.read(info.filename).decode("utf-8", errors="replace")
            except Exception:
                target = "<unreadable>"
            symlinks.append((info.filename, target))
    return symlinks


def find_missing_exec_bits(zf: zipfile.ZipFile) -> list[tuple[str, int]]:
    """Returns a list of (filename, mode) for binaries missing executable permission bits."""
    missing: list[tuple[str, int]] = []
    for info in zf.infolist():
        mode = info.external_attr >> 16
        # Skip entries that do not carry POSIX file mode attributes
        if not mode:
            continue
        if (
            info.filename in KNOWN_EXECUTABLES
            or info.filename.startswith("emulator/bin/")
        ) and not info.filename.endswith("/"):
            if not (mode & 0o111):
                missing.append((info.filename, mode))
    return missing


def _resolve_path(path: str) -> str:
    """Resolves path using Bazel runfiles."""
    runfiles = Runfiles.Create()
    return runfiles.Rlocation(path)


class ReleaseZipTest(unittest.TestCase):
    """Integration test verifying contents and packaging integrity of the release zip."""

    def test_contains_expected_files(self):
        self.assertNotEqual(ARGS.expected_path, "", msg="--expected_path must be provided")
        self.assertNotEqual(ARGS.release_zip_path, "", msg="--release_zip_path must be provided")
        expected_path = _resolve_path(ARGS.expected_path)
        zip_path = _resolve_path(ARGS.release_zip_path)
        with open(expected_path) as ef:
            expected = set(l.strip() for l in ef)
        with zipfile.ZipFile(zip_path) as zf:
            got = set(zf.namelist())
        error_msgs = []
        missing = expected - got
        if missing:
            error_msgs.append("Missing files: " + ','.join(sorted(missing)))
        unexpected = got - expected
        if unexpected:
            error_msgs.append("Unexpected files: " + ','.join(sorted(unexpected)))
        if error_msgs:
            self.fail("\n".join(error_msgs))

    def test_directory_permissions(self):
        self.assertNotEqual(ARGS.release_zip_path, "", msg="--release_zip_path must be provided")
        zip_path = _resolve_path(ARGS.release_zip_path)
        bad_dirs = []
        with zipfile.ZipFile(zip_path) as zf:
            for info in zf.infolist():
                if info.filename.endswith("/"):
                    mode = (info.external_attr >> 16) & 0o7777
                    if mode != 0o755:
                        bad_dirs.append(f"{info.filename} (mode: {oct(mode)})")
        if bad_dirs:
            self.fail(
                "Directories in release zip must have 0o755 permissions:\n  "
                + "\n  ".join(sorted(bad_dirs))
            )

    def test_does_not_contain_symlinks(self):
        """Verifies that the release archive contains no symbolic links."""
        self.assertNotEqual(
            ARGS.release_zip_path, "", msg="--release_zip_path must be provided"
        )
        zip_path = _resolve_path(ARGS.release_zip_path)
        with zipfile.ZipFile(zip_path) as zf:
            symlinks = find_symlinks(zf)

        if symlinks:
            sample = "\n  ".join(f"{name} -> {target}" for name, target in symlinks[:20])
            total = len(symlinks)
            suffix = f"\n  ... and {total - 20} more" if total > 20 else ""
            self.fail(
                f"Release archive contains {total} symbolic link(s) "
                f"(expected regular files only):\n  {sample}{suffix}"
            )

    def test_executables_have_exec_bit(self):
        """Verifies that primary binaries retain executable permissions when POSIX mode is present."""
        if sys.platform == "win32":
            return
        self.assertNotEqual(
            ARGS.release_zip_path, "", msg="--release_zip_path must be provided"
        )
        zip_path = _resolve_path(ARGS.release_zip_path)
        with zipfile.ZipFile(zip_path) as zf:
            missing = find_missing_exec_bits(zf)

        if missing:
            sample = "\n  ".join(f"{name} (mode {oct(mode)})" for name, mode in missing)
            self.fail(
                f"Release archive binaries missing executable bit:\n  {sample}"
            )


def parse_args():
    """Parses the test arguments, removing these from sys.argv."""
    parser = argparse.ArgumentParser(
        description="Test the release zip contains the expected files."
    )
    parser.add_argument(
        "--release_zip_path",
        help="The path to the release zip to inspect.",
    )
    parser.add_argument(
        "--expected_path",
        help="Path to the list of expected files.",
    )
    global ARGS
    ARGS, extra_args = parser.parse_known_args()
    sys.argv = sys.argv[:1] + extra_args


if __name__ == "__main__":
    parse_args()
    unittest.main()

