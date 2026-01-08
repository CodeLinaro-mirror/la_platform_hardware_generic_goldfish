import argparse
import sys
import unittest
import zipfile

from python.runfiles import Runfiles

ARGS = argparse.Namespace()


class ReleaseZipTest(unittest.TestCase):

    def test_contains_expected_files(self):
        self.assertNotEqual(ARGS.expected_path, "", msg="--expected_path must be provided")
        self.assertNotEqual(ARGS.release_zip_path, "", msg="--release_zip_path must be provided")
        runfiles = Runfiles.Create()
        with open(runfiles.Rlocation(ARGS.expected_path)) as ef:
            expected = set(l.strip() for l in ef)
        with zipfile.ZipFile(runfiles.Rlocation(ARGS.release_zip_path)) as zf:
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
