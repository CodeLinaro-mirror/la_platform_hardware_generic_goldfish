#!/usr/bin/env python3
# Copyright 2025 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# Minimal dependency script to query, download, and manage Android resources.

import argparse
import hashlib
import logging
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
import urllib.request
import urllib.error

# --- Configuration ---
CONFIG = {"SCHEME": "https://"}


class ChecksumValidationError(Exception):
    """Raised when a downloaded file's checksum does not match the expected value."""

    pass


# --- Constants ---
REPO_URL_TEMPLATES = {
    "sysimg": "{SCHEME}dl.google.com/android/repository/sys-img/{TAG}/sys-img2-1.xml",
    "sysimg_download": "{SCHEME}dl.google.com/android/repository/sys-img/{TAG}/{ZIP}",
    "full_repo": "{SCHEME}dl.google.com/android/repository/repository2-1.xml",
    "emu_download": "{SCHEME}dl.google.com/android/repository/{PATH}",
}
SYSIMG_TAGS = [
    "android",
    "google_apis",
    "google_apis_playstore",
    "google_atd",
    "android-tv",
]
CHANNEL_MAPPING = {
    "channel-0": "stable",
    "channel-1": "beta",
    "channel-2": "dev",
    "channel-3": "canary",
}
API_LETTER_MAPPING = {
    "10": "G",
    "15": "I",
    "16": "J",
    "17": "J",
    "18": "J",
    "19": "K",
    "21": "L",
    "22": "L",
    "23": "M",
    "24": "N",
    "25": "N",
    "26": "O",
    "27": "O",
    "28": "P",
    "29": "Q",
    "30": "R",
    "31": "S",
    "32": "S",
    "33": "T",
    "34": "U",
    "35": "V",
    "36": "B",
}


def _validate_checksum(file_path: Path, expected_checksum: str):
    """Validates that the SHA1 checksum of a file matches the expected value."""
    if not expected_checksum:
        logging.warning(
            f"No checksum provided for {file_path.name}. Skipping validation."
        )
        return
    logging.info(f"Validating checksum for {file_path.name}...")
    hasher = hashlib.sha1()
    with open(file_path, "rb") as f:
        while chunk := f.read(8192):
            hasher.update(chunk)
    actual_checksum = hasher.hexdigest()
    if actual_checksum.lower() != expected_checksum.lower():
        raise ChecksumValidationError(
            f"Checksum validation FAILED for {file_path.name}!\n"
            f"  Expected: {expected_checksum}\n"
            f"  Actual:   {actual_checksum}"
        )
    logging.info("Checksum validation passed.")


def download(url: str, dest_path: Path, expected_checksum: str = None) -> Path:
    """Downloads a file from a URL to a specified destination, validating the checksum if provided."""
    logging.info(f"Downloading {url} to {dest_path}...")
    try:
        dest_path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=300) as response, open(dest_path, "wb") as out_file:
            total_size = int(response.headers.get("content-length", 0))
            bytes_so_far = 0
            while True:
                chunk = response.read(8192)
                if not chunk:
                    break
                bytes_so_far += len(chunk)
                out_file.write(chunk)
                if total_size:
                    progress = (bytes_so_far / total_size) * 100
                    sys.stderr.write(f"\r -> Progress: {progress:3.0f}%")
                    sys.stderr.flush()
        sys.stderr.write("\n")
        logging.info(f"Successfully downloaded {dest_path.name}.")
        _validate_checksum(dest_path, expected_checksum)
    except ChecksumValidationError as e:
        if dest_path.exists():
            dest_path.unlink()
        raise e
    except urllib.error.URLError as e:
        sys.stderr.write("\n")
        if dest_path.exists():
            dest_path.unlink()
        raise e
    return dest_path


def _parse_revision(rev_element):
    """Parses the revision information from an XML element."""
    if rev_element is None:
        return "0", (0,)
    parts = [
        int(el.text) if el is not None and el.text is not None else 0
        for el in [
            rev_element.find("major"),
            rev_element.find("minor"),
            rev_element.find("micro"),
        ]
    ]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return ".".join(map(str, parts)), tuple(parts)


class SysImgInfo:
    """Represents information about a system image, parsed from the repository XML."""

    def __init__(self, pkg, tag_source):
        details = pkg.find("type-details")
        self.api = details.find("api-level").text
        self.tag = tag_source
        self.abi = details.find("abi").text
        self.revision_str, self.revision_tuple = _parse_revision(pkg.find("revision"))
        archive = pkg.find(".//archive/complete")
        self.checksum = archive.find("checksum").text if archive is not None else None
        codename = details.find("codename")
        self.letter = (
            codename.text
            if codename is not None
            else API_LETTER_MAPPING.get(self.api, "A")
        )
        url_element = pkg.find(".//archive[host-os='linux']/complete/url") or pkg.find(
            ".//url"
        )
        self.zip = url_element.text
        self.url = REPO_URL_TEMPLATES["sysimg_download"].format(
            SCHEME=CONFIG["SCHEME"], TAG=self.tag, ZIP=self.zip
        )

    def download_name(self):
        return f"sys-img-{self.tag}-{self.api}-{self.letter}-{self.abi}-r{self.revision_str}.zip"

    def download(self, dest_path: Path = None):
        full_dest_path = dest_path if dest_path else Path.cwd() / self.download_name()
        return download(self.url, full_dest_path, expected_checksum=self.checksum)

    def __str__(self):
        return f"API: {self.api:<3} Tag: {self.tag:<22} ABI: {self.abi:<12} Revision: {self.revision_str}"


class EmuInfo:
    """Represents information about an emulator, parsed from the repository XML."""

    def __init__(self, pkg):
        rev = pkg.find("revision")
        self.version = f"{rev.find('major').text}.{rev.find('minor').text}.{rev.find('micro').text}"
        channel = pkg.find("channelRef")
        self.channel = CHANNEL_MAPPING.get(channel.attrib["ref"], "unknown")
        self.urls = {}
        for archive in pkg.find("archives"):
            url_path = archive.find(".//url").text
            hostos = archive.find("host-os").text
            self.urls[hostos] = REPO_URL_TEMPLATES["emu_download"].format(
                SCHEME=CONFIG["SCHEME"], PATH=url_path
            )

    def download_name(self):
        return f"emulator-{self.version}.zip"

    def download(self, hostos="linux", dest_path: Path = None):
        full_dest_path = dest_path if dest_path else Path.cwd() / self.download_name()
        return download(self.urls[hostos], full_dest_path)

    def __str__(self):
        return f"Channel: {self.channel:<8} Version: {self.version:<15}"


def _get_repo_xml(url):
    """Fetches and returns the content of a repository XML file from the given URL."""
    logging.debug(f"Fetching repository XML from {url}")
    try:
        with urllib.request.urlopen(url) as response:
            if response.status == 200:
                return response.read()
            else:
                logging.warning(f"Failed to fetch {url}: status code {response.status}")
    except urllib.error.URLError as e:
        logging.error(f"Failed to fetch {url}: {e}")
    return None


def get_images_info():
    """Retrieves information about available system images from the repository XML files."""
    all_infos = []
    for tag in SYSIMG_TAGS:
        url = REPO_URL_TEMPLATES["sysimg"].format(SCHEME=CONFIG["SCHEME"], TAG=tag)
        xml_content = _get_repo_xml(url)
        if not xml_content:
            continue
        root = ET.fromstring(xml_content)
        for pkg in root.findall("remotePackage"):
            all_infos.append(SysImgInfo(pkg, tag))
    return all_infos


def get_emus_info():
    """Retrieves information about available emulators from the repository XML file."""
    url = REPO_URL_TEMPLATES["full_repo"].format(SCHEME=CONFIG["SCHEME"])
    xml_content = _get_repo_xml(url)
    if not xml_content:
        return []
    root = ET.fromstring(xml_content)
    infos = [
        EmuInfo(p)
        for p in root.findall("remotePackage")
        if p.attrib.get("path") == "emulator"
    ]
    return infos


def handle_list(args):
    logging.info("Querying available downloads...")
    if not args.no_system_images:
        logging.info("--- Available System Images ---")
        img_infos = get_images_info()
        if not img_infos:
            logging.warning("Could not retrieve any system images.")
        for info in sorted(
            img_infos, key=lambda i: (int(i.api), i.tag, i.abi, i.revision_tuple)
        ):
            logging.info(info)
    if args.emulator:
        logging.info("--- Available Emulators ---")
        emu_infos = get_emus_info()
        if not emu_infos:
            logging.warning("Could not retrieve any emulators.")
        for info in sorted(emu_infos, key=lambda e: (e.channel, e.version)):
            logging.info(info)


def handle_sysimg_download(args):
    logging.info(
        f"Searching for system image: API={args.api}, ABI={args.abi}, Tag={args.tag}, Revision={args.revision or 'latest'}"
    )
    all_images = get_images_info()
    candidates = [
        img
        for img in all_images
        if img.api == args.api and img.abi == args.abi and img.tag == args.tag
    ]
    if not candidates:
        raise FileNotFoundError(
            "No matching system image found for the specified criteria."
        )
    if args.revision:
        target_image = next(
            (img for img in candidates if img.revision_str == args.revision), None
        )
        if not target_image:
            available = [c.revision_str for c in candidates]
            raise FileNotFoundError(
                f"Revision '{args.revision}' not found. Available revisions: {available}"
            )
    else:
        target_image = sorted(candidates, key=lambda i: i.revision_tuple, reverse=True)[
            0
        ]
        logging.info(
            f"No revision specified. Selected latest: {target_image.revision_str}"
        )
    logging.info(f"Found matching image: {target_image}")
    dest_path = Path(args.out) if args.out else None
    target_image.download(dest_path=dest_path)


def handle_emu_download(args):
    logging.info(f"Attempting to find latest emulator in '{args.channel}' channel...")
    emulators = get_emus_info()
    channel_emulators = sorted(
        [e for e in emulators if e.channel == args.channel and "linux" in e.urls],
        key=lambda e: list(map(int, e.version.split("."))),
        reverse=True,
    )
    if not channel_emulators:
        raise FileNotFoundError(
            f"No Linux emulator found in the '{args.channel}' channel."
        )
    latest_emu = channel_emulators[0]
    logging.info(f"Found latest emulator: {latest_emu}")
    dest_path = Path(args.out) if args.out else None
    latest_emu.download(hostos="linux", dest_path=dest_path)


def handle_generate_bzl(args):
    """Queries repositories and prints a valid Starlark list of the LATEST revision for each image."""
    logging.info("Querying all system images to find the latest revision for each...")

    all_images = get_images_info()
    latest_images = {}

    # --- Find the latest revision for each image type ---
    for image in all_images:
        if int(image.api) < 33:
            continue
        key = (image.api, image.tag, image.abi)
        if (
            key not in latest_images
            or image.revision_tuple > latest_images[key].revision_tuple
        ):
            latest_images[key] = image

    sorted_images = sorted(
        list(latest_images.values()), key=lambda i: (int(i.api), i.tag, i.abi)
    )

    # --- Find the overall latest google_apis_playstore images ---
    playstore_images = [img for img in all_images if img.tag == "google_apis_playstore"]

    x64_candidates = sorted(
        [img for img in playstore_images if img.abi == "x86_64"],
        key=lambda i: (int(i.api), i.revision_tuple),
        reverse=True,
    )
    arm64_candidates = sorted(
        [img for img in playstore_images if img.abi == "arm64-v8a"],
        key=lambda i: (int(i.api), i.revision_tuple),
        reverse=True,
    )

    if not x64_candidates:
        raise ValueError(
            "Could not find any 'google_apis_playstore' x86_64 image to set LATEST_API_X64."
        )
    if not arm64_candidates:
        raise ValueError(
            "Could not find any 'google_apis_playstore' arm64-v8a image to set LATEST_API_ARM64."
        )

    latest_x64 = x64_candidates[0]
    latest_arm64 = arm64_candidates[0]

    latest_x64_label = f":sys_{latest_x64.api}_{latest_x64.tag}_{latest_x64.abi}"
    latest_arm64_label = f":sys_{latest_arm64.api}_{latest_arm64.tag}_{latest_arm64.abi}"

    # --- Print the entire BZL file to stdout ---
    print('"""')
    print("This file contains a list of all supported Android system images,")
    print("pinned to the latest known revision for each type.")
    print("It is used by the BUILD file to auto-generate download targets.")
    print("")
    print("This file is auto-generated. To update, run:")
    print("  ./download_android_resource.py generate-bzl > images.bzl")
    print('"""')
    print("")
    print("SYSTEM_IMAGES = [")
    for image in sorted_images:
        print(
            f'    {{"api": "{image.api}", "tag": "{image.tag}", "abi": "{image.abi}", "rev": "{image.revision_str}"}},'
        )
    print("]")
    print("")
    print("# The latest available Google Play images.")
    print(f'LATEST_API_X64 = "{latest_x64_label}"')
    print(f'LATEST_API_ARM64 = "{latest_arm64_label}"')

# --- Main Execution ---


def main():
    """Sets up argument parsing and launches the correct command handler."""
    parser = argparse.ArgumentParser(
        description="A tool to query, download, and manage Android resources.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog="""
Examples:
  # List all available system images and emulators
  %(prog)s list

  # List only emulators
  %(prog)s list --emulator

  # Download the latest revision of a specific system image
  %(prog)s sysimg --api 34 --tag google_apis --abi x86_64

  # Download a pinned revision of a system image to a specific file
  %(prog)s sysimg --api 33 --tag android-tv --abi x86 --revision 5 --out /tmp/tv.zip

  # Download the latest stable emulator
  %(prog)s emu --channel stable --out stable_emu.zip

  # Generate the images.bzl file for Bazel, pinning to the latest known revisions
  %(prog)s generate-bzl > images.bzl
""",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose (DEBUG) logging."
    )
    parser.add_argument(
        "--http",
        action="store_true",
        help="Use HTTP instead of HTTPS for all downloads.",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    # List command
    list_parser = subparsers.add_parser(
        "list", help="List available emulators and/or system images."
    )
    list_parser.add_argument(
        "--emulator", action="store_true", help="List only emulators."
    )
    list_parser.add_argument(
        "--no-system-images", action="store_true", help="Do not list system images."
    )
    list_parser.set_defaults(func=handle_list)

    # System Image Download command
    sysimg_parser = subparsers.add_parser(
        "sysimg", help="Download a specific system image."
    )
    sysimg_parser.add_argument(
        "--api", required=True, help="The API level of the system image (e.g., '31')."
    )
    sysimg_parser.add_argument(
        "--tag", required=True, choices=SYSIMG_TAGS, help="The tag of the system image."
    )
    sysimg_parser.add_argument(
        "--abi", required=True, help="The ABI of the system image (e.g., x86_64)."
    )
    sysimg_parser.add_argument(
        "--revision",
        help="Specific revision of the system image.\nIf omitted, the latest is downloaded.",
    )
    sysimg_parser.add_argument(
        "--out", help="Full path and filename for the downloaded file."
    )
    sysimg_parser.set_defaults(func=handle_sysimg_download)

    # Emulator Download command
    emu_parser = subparsers.add_parser(
        "emu", help="Download the latest emulator for a specific channel."
    )
    emu_parser.add_argument(
        "--channel",
        default="stable",
        choices=["stable", "beta", "dev", "canary"],
        help="The channel to download from.",
    )
    emu_parser.add_argument(
        "--out", help="Full path and filename for the downloaded file."
    )
    emu_parser.set_defaults(func=handle_emu_download)

    # --- NEW: generate-bzl command parser ---
    generate_parser = subparsers.add_parser(
        "generate-bzl",
        help="Generate a Starlark list of all supported system images (API 33+).",
    )
    generate_parser.set_defaults(func=handle_generate_bzl)

    args = parser.parse_args()

    # Configure logging to stderr, so stdout can be used for redirection.
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level, format="%(levelname)s: %(message)s", stream=sys.stderr
    )

    if args.http:
        CONFIG["SCHEME"] = "http://"

    try:
        args.func(args)
    except Exception as e:
        logging.fatal(e, exc_info=args.verbose)  # Show traceback only in verbose mode
        sys.exit(1)


if __name__ == "__main__":
    main()
