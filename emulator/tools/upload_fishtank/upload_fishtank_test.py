import unittest
from unittest.mock import MagicMock, patch
import sys

# Mock aemu modules before importing upload_fishtank
sys.modules["ab"] = MagicMock()
sys.modules["ab.android_build_client"] = MagicMock()
sys.modules["ab.fetch_artifact"] = MagicMock()
sys.modules["ab.log"] = MagicMock()

from pathlib import Path
import upload_fishtank


class TestFishtankUploader(unittest.TestCase):
    def test_target_mapping(self):
        """Verify that platforms are mapped to the correct build targets and artifacts."""
        version = "12345"
        expected = {
            "linux": (
                "emulator-linux_x64_gfxstream",
                f"FISHTANK-sdk-repo-linux-emu-{version}.zip",
            ),
            "mac": (
                "emulator-mac_aarch64_gfxstream",
                f"FISHTANK-sdk-repo-darwin_aarch64-emu-{version}.zip",
            ),
            "windows": (
                "emulator-windows_x64_gfxstream",
                f"FISHTANK-sdk-repo-windows-emu-{version}.zip",
            ),
        }
        for platform, (target, artifact) in expected.items():
            t, a = upload_fishtank.get_target_and_artifact(platform, version)
            self.assertEqual(t, target)
            self.assertEqual(a, artifact)

    @patch("upload_fishtank.subprocess.run")
    def test_upload_to_gcs(self, mock_run):
        """Verify the gcloud storage cp command is constructed correctly."""
        local_path = Path("/tmp/file.zip")
        version = "12345"
        upload_fishtank.upload_to_gcs(local_path, version)

        expected_dest = f"gs://emu-next-bazel/fishtank/{version}/file.zip"
        mock_run.assert_called_once_with(
            ["gcloud", "storage", "cp", str(local_path), expected_dest], check=True
        )

    def test_generate_bazel_snippet(self):
        """Verify the Bazel snippet format."""
        platform = "linux"
        sha256 = "deadbeef"
        url = "gs://bucket/file.zip"
        snippet = upload_fishtank.generate_bazel_snippet(platform, sha256, url)

        self.assertIn('name = "fishtank-linux"', snippet)
        self.assertIn(f'sha256 = "{sha256}"', snippet)
        self.assertIn(f'url = "{url}"', snippet)


if __name__ == "__main__":
    unittest.main()
