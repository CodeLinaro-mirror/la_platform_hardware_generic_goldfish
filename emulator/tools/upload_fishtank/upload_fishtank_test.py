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
            "linux_internal": (
                "emulator-linux_x64_gfxstream_internal",
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

        # Test standard upload
        upload_fishtank.upload_to_gcs(local_path, version, "linux")
        expected_dest = f"gs://emu-next-bazel/fishtank/{version}/file.zip"
        mock_run.assert_called_with(
            ["gcloud", "storage", "cp", str(local_path), expected_dest], check=True
        )

        # Test internal upload
        upload_fishtank.upload_to_gcs(local_path, version, "linux_internal")
        expected_internal_dest = f"gs://emu-next-bazel/fishtank/{version}/internal/file.zip"
        mock_run.assert_called_with(
            ["gcloud", "storage", "cp", str(local_path), expected_internal_dest], check=True
        )

    def test_generate_bazel_snippet(self):
        """Verify the Bazel snippet format."""
        platform = "linux"
        sha256 = "deadbeef"
        url = "gs://bucket/file.zip"
        snippet = upload_fishtank.generate_bazel_snippet(platform, sha256, url)

        self.assertIn('multisource_repo.file(', snippet)
        self.assertIn('name = "fishtank-linux"', snippet)
        self.assertIn('"local_file": "@goldfish_build//utils:empty.zip"', snippet)
        self.assertIn(f'"sha256": "{sha256}"', snippet)
        self.assertIn(f'"url": "{url}"', snippet)


if __name__ == "__main__":
    unittest.main()
