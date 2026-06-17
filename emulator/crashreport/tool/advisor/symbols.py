# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Manages symbol retrieval exclusively via Android Build (go/ab) pre-bundled zip archives."""

import getpass
import logging
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Optional

from ab.android_build_client import AndroidBuildClient
from api import CrashApi
from metadata import CrashMetadata


class SymbolFetcher:
    """Manages symbol retrieval exclusively via Android Build (go/ab) pre-bundled zip archives."""

    def __init__(self, api: CrashApi, global_cache_dir: Optional[str] = None) -> None:
        self.api = api
        if not global_cache_dir:
            user = getpass.getuser()
            global_cache_dir = (
                f"{tempfile.gettempdir()}/crashadvisor_symbols_cache_{user}"
            )
        self.global_cache = Path(global_cache_dir)
        self.global_cache.mkdir(parents=True, exist_ok=True)

    def _obtain_oauth_token(self, custom_token: Optional[str]) -> Optional[str]:
        """Obtain OAuth2 token supporting --token flag, --sso, and non-SSO browser fallback."""
        if custom_token:
            return custom_token.strip()

        # Try standard AndroidBuildClient helper first (which uses --sso)
        try:
            token = AndroidBuildClient.obtain_token(None)
            if token:
                return token.strip()
        except Exception as e:
            logging.debug("AndroidBuildClient.obtain_token (--sso) failed: %s", e)

        # Fallback for macOS / non-SSO environments: use plain 'oauth2l fetch androidbuild.internal'
        oauth2l_path = shutil.which("oauth2l") or shutil.which(
            "oauth2l", path=Path.home() / "go" / "bin"
        )
        if oauth2l_path:
            logging.info("Attempting OAuth2 token retrieval without --sso...")
            try:
                token = subprocess.check_output(
                    [oauth2l_path, "fetch", "androidbuild.internal"],
                    timeout=30,
                    text=True,
                )
                if token:
                    return token.strip()
            except subprocess.CalledProcessError as e:
                logging.debug("Plain oauth2l fetch failed: %s", e)
            except subprocess.TimeoutExpired:
                logging.warning("oauth2l fetch timed out waiting for browser login.")

        return None

    def fetch_symbols(
        self,
        metadata: CrashMetadata,
        symbols_dir: Path,
        custom_token: Optional[str] = None,
    ) -> None:
        """Fetch complete Breakpad symbols zip exclusively from Android Build."""
        logging.info("Starting symbol discovery pipeline via Android Build (go/ab)...")
        symbols_dir.mkdir(parents=True, exist_ok=True)

        build_id = metadata.build_id
        build_target = metadata.build_target
        logging.info("Target Android Build ID: %s (Target: %s)", build_id, build_target)

        if not build_id:
            raise ValueError(
                "No Build ID found in metadata. Cannot fetch symbols from Android Build."
            )

        platform_str = build_target.replace("emulator_", "").replace("emulator-", "")
        if platform_str == "linux_x64":
            platform_str = "linux"
        elif platform_str == "windows_x64":
            platform_str = "windows"
        elif platform_str == "mac_x64":
            platform_str = "mac"

        artifact_name = (
            f"sdk-repo-{platform_str}-emulator-breakpad-symbols-{build_id}.zip"
        )
        build_cache_dir = self.global_cache / str(build_id)
        build_cache_dir.mkdir(parents=True, exist_ok=True)
        global_zip_path = build_cache_dir / artifact_name

        logging.info("Checking global symbols cache at: %s", global_zip_path)
        if not global_zip_path.exists():
            logging.info(
                "Artifact not in global cache. Attempting high-speed download from Android Build (go/ab)..."
            )
            token = self._obtain_oauth_token(custom_token)

            # Early Token & Scope Validation
            try:
                ab_client = AndroidBuildClient(token)
                logging.info(
                    "Querying available artifacts in go/ab for build %s (target: %s)...",
                    build_id,
                    build_target,
                )
                artifacts = list(ab_client.list_artifacts(build_id, build_target))
                matching = [
                    a
                    for a in artifacts
                    if ("breakpad-symbols" in a or "emulator-symbols" in a)
                    and a.endswith(".zip")
                ]
                if not matching:
                    raise FileNotFoundError(
                        f"No breakpad or emulator symbols zip artifact found in go/ab for build {build_id} (target: {build_target}). Available artifacts: {artifacts}"
                    )
                actual_artifact = matching[0]
                global_zip_path = build_cache_dir / actual_artifact

                if not global_zip_path.exists():
                    logging.info(
                        "Downloading artifact %s from go/ab to global cache...",
                        actual_artifact,
                    )
                    ab_client.fetch_bits(
                        str(global_zip_path), build_id, build_target, actual_artifact
                    )
                    logging.info(
                        "Successfully downloaded symbols zip to global cache: %s",
                        global_zip_path,
                    )
                else:
                    logging.info(
                        "Actual symbols zip already cached globally at: %s", global_zip_path
                    )
            except Exception as e:
                err_msg = (
                    f"Android Build symbol retrieval failed: {e}\n"
                    "=== OAuth2 Token & Scope Validation Failure ===\n"
                    "Android Build API requires a valid OAuth2 token with API_ANDROID_BUILD_INTERNAL scope.\n"
                )
                if platform.system() == "Darwin":
                    err_msg += (
                        "On macOS, plain 'oauth2l fetch androidbuild.internal' grants cloud-platform scope, which is insufficient (HTTP 403/401).\n"
                        "To enable high-speed Android Build zip downloading on macOS, generate an SSO token on a GLinux workstation:\n"
                        "  oauth2l fetch --sso $USER@google.com androidbuild.internal\n\n"
                        "Then pass the generated token to this tool using the --token flag:\n"
                        "  bazel run @goldfish//emulator/crashreport/tool/advisor -- <crash_id> --token <generated_token>\n"
                    )
                else:
                    err_msg += "Ensure you have active LOAS credentials (gcert) or pass a valid token via --token.\n"
                err_msg += "==============================================="
                raise RuntimeError(err_msg) from e
        else:
            logging.info("Symbols zip already cached globally at: %s", global_zip_path)

        if global_zip_path.exists():
            logging.info(
                "Unpacking breakpad symbols zip from global cache into %s...",
                symbols_dir,
            )
            shutil.unpack_archive(str(global_zip_path), str(symbols_dir))
            logging.info("Symbols successfully extracted from global cache.")
