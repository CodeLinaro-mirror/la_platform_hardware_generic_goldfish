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

# IMPORTANT: You may need to run "gpkg setup" before running the following bazel command.
# Run with: bazel run @goldfish//emulator/tools/upload_fishtank:upload_fishtank

import argparse
import hashlib
import logging
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from python.runfiles import Runfiles
from ab.android_build_client import AndroidBuildClient
from ab.fetch_artifact import fetch_artifact
from ab.log import configure_logging

GCS_BUCKET_TEMPLATE = "gs://emu-next-bazel/fishtank/{version}/{zipfile}"

_MODULE_BAZEL_RUNFILE_PATH = "goldfish_build+/registry/modules/goldfish/0.0.1/MODULE.bazel"


def get_target_and_artifact(platform_name, version):
    """Maps platform to Android Build target and artifact name."""
    mapping = {
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
    return mapping.get(platform_name)


def get_gcs_url(version, platform, zipfile_name):
    """Constructs the GCS destination URL."""
    if platform == "linux_internal":
        zipfile_name = f"internal/{zipfile_name}"
    return GCS_BUCKET_TEMPLATE.format(version=version, zipfile=zipfile_name)


def calculate_sha256(file_path):
    """Calculates the SHA256 hash of a local file."""
    logging.info("Calculating SHA256 for %s...", file_path.name)
    hasher = hashlib.sha256()
    with open(file_path, "rb") as f:
        while chunk := f.read(8192):
            hasher.update(chunk)
    return hasher.hexdigest()


def calculate_sha256_gcs(gcs_url):
    """Calculates the SHA256 hash of a GCS file by streaming it."""
    logging.info("Calculating SHA256 for existing GCS file %s...", gcs_url)
    hasher = hashlib.sha256()
    process = subprocess.Popen(["gcloud", "storage", "cat", gcs_url], stdout=subprocess.PIPE)
    for chunk in iter(lambda: process.stdout.read(8192), b""):
        hasher.update(chunk)
    process.wait()
    if process.returncode != 0:
        raise Exception(f"Failed to read from GCS: {gcs_url}")
    return hasher.hexdigest()


def upload_to_gcs(local_path, version, platform):
    """Uploads a file to GCS using gcloud storage cp."""
    dest = get_gcs_url(version, platform, local_path.name)
    logging.info("Uploading %s to %s...", local_path.name, dest)
    subprocess.run(["gcloud", "storage", "cp", str(local_path), dest], check=True)
    return dest


def generate_bazel_snippet(platform_name, sha256, url):
    """Generates a multisource_repo.file Bazel snippet."""
    return f"""multisource_repo.file(
    name = "fishtank-{platform_name}",
    aosp = {{
        "local_file": "@goldfish_build//utils:empty.zip",
    }},
    goog = {{
        "sha256": "{sha256}",
        "url": "{url}",
    }},
)"""


def update_module_bazel(snippets_text):
    """Updates the MODULE.bazel file with the generated snippets."""
    runfiles = Runfiles.Create()
    module_bazel_path = Path(runfiles.Rlocation(_MODULE_BAZEL_RUNFILE_PATH))

    content = module_bazel_path.read_text()
    pattern = r'(# BEGIN upload_fishtank\n).*?(# END upload_fishtank)'

    if re.search(pattern, content, re.DOTALL):
        def repl(match):
            return f"{match.group(1)}{snippets_text}\n{match.group(2)}"
        new_content = re.sub(pattern, repl, content, flags=re.DOTALL)
        module_bazel_path.write_text(new_content)
        logging.info("Updated file: %s", module_bazel_path)
    else:
        logging.warning("Could not find # BEGIN upload_fishtank scope in %s to update.", module_bazel_path)


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

    platforms = ["linux", "linux_internal", "mac", "windows"]
    snippets = []

    # Initialize AB Client lazily in case all artifacts already exist in GCS
    ab_client = None

    with tempfile.TemporaryDirectory(prefix="fishtank_download_") as tmp_dir:
        tmp_path = Path(tmp_dir)
        for platform in platforms:
            target, artifact = get_target_and_artifact(platform, args.version)
            logging.info(
                "Processing %s: target=%s, artifact=%s", platform, target, artifact
            )

            try:
                gcs_url = get_gcs_url(args.version, platform, artifact)

                # Check if the artifact already exists in GCS
                check_cmd = subprocess.run(
                    ["gcloud", "storage", "ls", gcs_url],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL
                )

                if check_cmd.returncode == 0:
                    logging.info("Artifact already exists in GCS: %s. Skipping download.", gcs_url)
                    sha256 = calculate_sha256_gcs(gcs_url)
                else:
                    if not ab_client:
                        try:
                            ab_client = AndroidBuildClient(args.token)
                        except Exception as e:
                            logging.error("Failed to initialize Android Build Client: %s", e)
                            sys.exit(1)

                    local_file = fetch_artifact(
                        ab_client, tmp_path, target, artifact, args.version
                    )
                    sha256 = calculate_sha256(local_file)
                    upload_to_gcs(local_file, args.version, platform)

                snippets.append(generate_bazel_snippet(platform, sha256, gcs_url))

            except Exception as e:
                logging.error("Failed to process %s: %s", platform, e)
                # We continue to other platforms even if one fails
                continue

    if snippets:
        snippets_text = "\n".join(snippets)
        print("\nGenerated Bazel Snippets:\n")
        print(snippets_text)
        update_module_bazel(snippets_text)
    else:
        logging.error("No snippets were generated.")
        sys.exit(1)


if __name__ == "__main__":
    main()