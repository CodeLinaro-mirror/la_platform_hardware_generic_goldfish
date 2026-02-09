#!/usr/bin/env python3
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

# Run with: bazel run @goldfish//fishtank_uploader:upload_fishtank

import argparse
import hashlib
import logging
import subprocess
import sys
import tempfile
from pathlib import Path

from ab.android_build_client import AndroidBuildClient
from ab.fetch_artifact import fetch_artifact
from ab.log import configure_logging

GCS_BUCKET_TEMPLATE = "gs://emu-next-bazel/fishtank/{version}/{zipfile}"


def get_target_and_artifact(platform_name, version):
    """Maps platform to Android Build target and artifact name."""
    mapping = {
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
    return mapping.get(platform_name)


def calculate_sha256(file_path):
    """Calculates the SHA256 hash of a file."""
    logging.info("Calculating SHA256 for %s...", file_path.name)
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(8192):
            hasher.update(chunk)
    return hasher.hexdigest()


def upload_to_gcs(local_path, version):
    """Uploads a file to GCS using gcloud storage cp."""
    dest = GCS_BUCKET_TEMPLATE.format(version=version, zipfile=local_path.name)
    logging.info("Uploading %s to %s...", local_path.name, dest)
    subprocess.run(["gcloud", "storage", "cp", str(local_path), dest], check=True)
    return dest


def generate_bazel_snippet(platform_name, sha256, url):
    """Generates a gcs_file Bazel snippet."""
    return f"""gcs_file(
    name = "fishtank-{platform_name}",
    sha256 = "{sha256}",
    url = "{url}",
)"""


def main():
    # go get github.com/google/oauth2l && go install github.com/google/oauth21
    logging.warning('You may need to run "~/go/bin/oauth2l reset"')

    parser = argparse.ArgumentParser(
        description="Download FISHTANK artifacts, upload to GCS, and generate Bazel snippets."
    )
    parser.add_argument("version", help="Android Build version (Build ID) to download.")
    parser.add_argument("--token", help="OAuth2 token for Android Build system.")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging."
    )
    args = parser.parse_args()

    lvl = logging.DEBUG if args.verbose else logging.INFO
    configure_logging(lvl)

    try:
        ab_client = AndroidBuildClient(args.token)
    except Exception as e:
        logging.error("Failed to initialize Android Build Client: %s", e)
        sys.exit(1)

    platforms = ["linux", "mac", "windows"]
    snippets = []

    with tempfile.TemporaryDirectory(prefix="fishtank_download_") as tmp_dir:
        tmp_path = Path(tmp_dir)
        for platform in platforms:
            target, artifact = get_target_and_artifact(platform, args.version)
            logging.info(
                "Processing %s: target=%s, artifact=%s", platform, target, artifact
            )

            try:
                local_file = fetch_artifact(
                    ab_client, tmp_path, target, artifact, args.version
                )
                sha256 = calculate_sha256(local_file)
                gcs_url = upload_to_gcs(local_file, args.version)
                snippets.append(generate_bazel_snippet(platform, sha256, gcs_url))
            except Exception as e:
                logging.error("Failed to process %s: %s", platform, e)
                # We continue to other platforms even if one fails
                continue

    if snippets:
        print("\nGenerated Bazel Snippets:\n")
        print("\n".join(snippets))
    else:
        logging.error("No snippets were generated.")
        sys.exit(1)


if __name__ == "__main__":
    main()
