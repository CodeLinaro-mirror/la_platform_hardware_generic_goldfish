# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Encapsulates execution of gosso with a stubby blade:crash-queryapi fallback."""

import base64
import json
import logging
import re
import shutil
import subprocess
import urllib.parse
from pathlib import Path
from typing import Optional

_SAFE_TOKEN_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
_CRASH_QUERYAPI_TARGET = "blade:crash-queryapi"


class GossoClient:
    """Fetches crash report artifacts via gosso with a stubby RPC fallback."""

    def __init__(
        self,
        gosso_path: Optional[str] = None,
        stubby_path: Optional[str] = None,
    ) -> None:
        self.gosso_path = (
            gosso_path
            or shutil.which("gosso")
            or shutil.which("gosso", path="/google/bin/releases/gosso")
        )
        self.stubby_path = stubby_path or shutil.which("stubby")
        if not self.gosso_path and not self.stubby_path:
            raise RuntimeError(
                "The 'gosso' binary was not found in PATH.\n"
                "gosso is required for non-interactive UberProxy authentication to fetch minidumps.\n"
                "Please install gosso (see http://go/gosso-doc) or manually download the minidump."
            )

    def fetch_url_to_file(self, url: str, output_path: Path) -> None:
        """Fetch an UberProxy protected URL to output_path, falling back to stubby on failure."""
        if self.gosso_path:
            logging.debug("Invoking gosso to fetch %s to %s", url, output_path)
            try:
                with open(output_path, "wb") as f:
                    subprocess.run(
                        [self.gosso_path, "-url", url],
                        stdout=f,
                        stderr=subprocess.PIPE,
                        check=True,
                    )
                logging.info("Successfully downloaded artifact to: %s", output_path)
                return
            except subprocess.CalledProcessError as e:
                err_msg = e.stderr.decode(errors="replace").strip() if e.stderr else str(e)
                logging.warning("Gosso execution failed (%s).", err_msg)
                if output_path.exists():
                    output_path.unlink()
                if self._try_fetch_via_stubby(url, output_path):
                    return
                raise
            except Exception as e:
                logging.error("Unexpected error during download: %s", e)
                if output_path.exists():
                    output_path.unlink()
                raise

        if self._try_fetch_via_stubby(url, output_path):
            return
        raise RuntimeError(f"Cannot fetch {url}: gosso is not available and URL is not supported by stubby fallback.")

    def _try_fetch_via_stubby(self, url: str, output_path: Path) -> bool:
        """Attempt to satisfy a crash.corp.google.com download via stubby blade:crash-queryapi."""
        if not self.stubby_path:
            return False

        parsed = urllib.parse.urlsplit(url)
        query = urllib.parse.parse_qs(parsed.query)

        try:
            if parsed.path.endswith("/get_report"):
                crash_id = (query.get("id") or [None])[0]
                if not crash_id:
                    return False
                logging.info(
                    "Falling back to stubby %s GetReport for crash ID %s...",
                    _CRASH_QUERYAPI_TARGET,
                    crash_id,
                )
                self.fetch_report_via_stubby(crash_id, output_path)
                return True

            if parsed.path.endswith("/fetch_file"):
                crash_id = (query.get("reportid") or [None])[0]
                filename = (query.get("filename") or ["upload_file_minidump"])[0]
                if not crash_id or not filename:
                    return False
                logging.info(
                    "Falling back to stubby %s GetFile (%s) for crash ID %s...",
                    _CRASH_QUERYAPI_TARGET,
                    filename,
                    crash_id,
                )
                self.fetch_file_via_stubby(crash_id, filename, output_path)
                return True
        except Exception as e:
            logging.error("Stubby fallback failed for %s: %s", url, e)
            if output_path.exists():
                output_path.unlink()

        return False

    def fetch_report_via_stubby(self, crash_id: str, output_path: Path) -> None:
        """Fetch CrashReportProto JSON via crash.api.QueryAPIService.GetReport."""
        if not self.stubby_path:
            raise RuntimeError("The 'stubby' binary was not found in PATH.")
        if not _SAFE_TOKEN_RE.match(crash_id):
            raise ValueError(f"Invalid crash_id: {crash_id}")

        req_proto = (
            f"report_id: '{crash_id}' "
            "allow_sensitive_report: true "
            "report_option: { option: ORIGINAL_REPORT_WITH_SHERLOCK }"
        )
        res = subprocess.run(
            [
                self.stubby_path,
                "--output_json",
                "call",
                _CRASH_QUERYAPI_TARGET,
                "crash.api.QueryAPIService.GetReport",
                req_proto,
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        payload = json.loads(res.stdout)
        status_code = payload.get("status", {}).get("status_code", -1)
        if status_code != 0:
            details = payload.get("status", {}).get("status_details", "")
            raise RuntimeError(
                f"QueryAPIService.GetReport returned status_code={status_code} ({details})"
            )

        report = payload.get("report", {})
        original_report = report.get("original_report")
        if not isinstance(original_report, dict):
            raise RuntimeError(
                f"QueryAPIService.GetReport returned no original_report for {crash_id}"
            )

        if "stable_signature" in original_report and "stableSignature" not in original_report:
            original_report["stableSignature"] = original_report["stable_signature"]

        wrapped = {
            "report_proto": original_report,
            "processing_state": report.get("processing_state"),
            "primes_ios_metric_extension_text_format": "",
        }
        output_path.write_text(json.dumps(wrapped), encoding="utf-8")
        logging.info("Successfully downloaded report metadata via stubby to: %s", output_path)

    def fetch_file_via_stubby(
        self, crash_id: str, filename: str, output_path: Path
    ) -> None:
        """Fetch a binary report attachment (e.g. upload_file_minidump) via QueryAPIService.GetFile."""
        if not self.stubby_path:
            raise RuntimeError("The 'stubby' binary was not found in PATH.")
        if not _SAFE_TOKEN_RE.match(crash_id):
            raise ValueError(f"Invalid crash_id: {crash_id}")
        if not _SAFE_TOKEN_RE.match(filename):
            raise ValueError(f"Invalid filename: {filename}")

        req_proto = f"report_id: '{crash_id}' file_field_name: '{filename}'"
        res = subprocess.run(
            [
                self.stubby_path,
                "--output_json",
                "call",
                _CRASH_QUERYAPI_TARGET,
                "crash.api.QueryAPIService.GetFile",
                req_proto,
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        payload = json.loads(res.stdout)
        status_code = payload.get("status", {}).get("status_code", -1)
        if status_code != 0:
            details = payload.get("status", {}).get("status_details", "")
            raise RuntimeError(
                f"QueryAPIService.GetFile returned status_code={status_code} ({details})"
            )

        files = payload.get("file", [])
        if not files or "contents" not in files[0]:
            raise RuntimeError(
                f"QueryAPIService.GetFile returned no file contents for {crash_id} ({filename})"
            )

        output_path.write_bytes(base64.b64decode(files[0]["contents"]))
        logging.info("Successfully downloaded %s via stubby to: %s", filename, output_path)

