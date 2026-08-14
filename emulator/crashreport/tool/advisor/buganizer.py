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

"""BuganizerClient: Cross-platform client for interacting with Google Issue Tracker.

Supports REST API backend (macOS/GWindows) and CLI wrapper backend (GLinux).
Includes automatic exponential backoff, stableSignature deduplication, and payload truncation.
"""

import json
import logging
import os
import random
import shutil
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Dict, List, Optional, Tuple

EMULATOR_COMPONENT_ID = 29601
PROD_REST_ENDPOINT = "https://issuetracker.googleapis.com/v1/issues"
QA_REST_ENDPOINT = "https://issuetracker.googleapis.com/v1/projects/b-qa/issues"

TAG_ANALYZED = "AI-CrashAdvisor-Analyzed"
TAG_DISPATCHED = "AI-Engineer-Dispatched"


class BuganizerError(Exception):
    """Base exception for Buganizer API failures."""


class BuganizerAuthError(BuganizerError):
    """Exception raised for HTTP 401 Unauthorized errors."""


class BuganizerClient:
    """Cross-platform client for managing Buganizer issues."""

    def __init__(
        self,
        token: Optional[str] = None,
        component_id: int = EMULATOR_COMPONENT_ID,
        qa_mode: bool = False,
        verbose: bool = False,
    ) -> None:
        self.token = token
        self.component_id = component_id
        self.qa_mode = qa_mode
        self.verbose = verbose
        self.cli_binary = self._resolve_cli_binary()
        self.base_url = QA_REST_ENDPOINT if qa_mode else PROD_REST_ENDPOINT

    def _resolve_cli_binary(self) -> Optional[str]:
        """Resolve base_cli or buganizer CLI binary if available (GLinux)."""
        for bin_name in ["buganizer", "base_cli"]:
            path = shutil.which(bin_name)
            if path:
                return path
        return None

    def _get_auth_token(self) -> str:
        """Fetch OAuth2 token from constructor or environment variables."""
        if self.token:
            return self.token
        env_token = os.environ.get("BUGANIZER_TOKEN")
        if env_token:
            return env_token
        # On GLinux with CLI binary, token may not be strictly required for CLI calls,
        # but for REST fallback we require a token.
        if self.cli_binary:
            return "cli_managed_token"
        raise BuganizerAuthError(
            "OAuth2 token missing. Please run:\n"
            "  oauth2l reset && oauth2l fetch --sso $USER@google.com "
            "https://www.googleapis.com/auth/buganizer https://www.googleapis.com/auth/androidbuild.internal\n"
            "and pass the token via --token or BUGANIZER_TOKEN environment variable."
        )

    def _truncate_payload(self, text: str, max_len: int = 50000) -> str:
        """Truncate large payloads to fit within Buganizer comment size limits."""
        if len(text) <= max_len:
            return text
        trunc_msg = "\n\n[Truncated for Buganizer size limits. Full dump in go/crash]"
        return text[: max_len - len(trunc_msg)] + trunc_msg

    def _execute_with_retry(
        self, func: Callable[..., Any], *args: Any, **kwargs: Any
    ) -> Any:
        """Execute a function with exponential backoff and jitter for 429/503 errors."""
        max_attempts = 5
        for attempt in range(max_attempts):
            try:
                return func(*args, **kwargs)
            except urllib.error.HTTPError as e:
                if e.code == 401:
                    raise BuganizerAuthError(
                        "OAuth2 token expired (HTTP 401). Please refresh your token via:\n"
                        "  oauth2l fetch --sso $USER@google.com https://www.googleapis.com/auth/buganizer\n"
                        "and re-run advisor.py. Execution will idempotently resume."
                    ) from e
                if e.code in (429, 503, 504):
                    if attempt == max_attempts - 1:
                        raise BuganizerError(
                            f"Max retries reached. HTTP {e.code}: {e.reason}"
                        ) from e
                    delay = (2**attempt) + random.uniform(0.1, 1.0)
                    logging.warning(
                        "HTTP %d received. Retrying in %.2fs (attempt %d/%d)",
                        e.code,
                        delay,
                        attempt + 1,
                        max_attempts,
                    )
                    time.sleep(delay)
                else:
                    err_body = e.read().decode("utf-8") if hasattr(e, "read") else ""
                    raise BuganizerError(
                        f"HTTP Error {e.code}: {e.reason}\nResponse Body: {err_body}"
                    ) from e
            except subprocess.CalledProcessError as e:
                if attempt == max_attempts - 1:
                    raise BuganizerError(f"CLI command failed: {e.stderr}") from e
                delay = (2**attempt) + random.uniform(0.1, 1.0)
                logging.warning(
                    "CLI execution failed. Retrying in %.2fs (attempt %d/%d)",
                    delay,
                    attempt + 1,
                    max_attempts,
                )
                time.sleep(delay)

    def _call_rest_api(
        self, url: str, method: str = "GET", payload: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        """Perform a direct REST API call using urllib."""
        token = self._get_auth_token()
        headers = {
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
            "User-Agent": "CrashAdvisor/1.0",
        }
        api_key = os.environ.get("BUGANIZER_API_KEY") or os.environ.get(
            "X_GOOG_API_KEY"
        )
        if api_key:
            headers["X-Goog-Api-Key"] = api_key
        user_project = os.environ.get("BUGANIZER_USER_PROJECT") or os.environ.get(
            "X_GOOG_USER_PROJECT"
        )
        if user_project:
            headers["X-Goog-User-Project"] = user_project
        data = json.dumps(payload).encode("utf-8") if payload is not None else None
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=30.0) as response:
            body = response.read().decode("utf-8")
            return json.loads(body) if body else {}

    def search_issue_by_signature(
        self, stable_signature: str
    ) -> Optional[Dict[str, Any]]:
        """Search Buganizer for an existing issue matching the stableSignature in the component."""

        def _search() -> Optional[Dict[str, Any]]:
            queries = [
                f'componentid:{self.component_id} title:"{stable_signature}"',
                f'componentid:{self.component_id} "{stable_signature}"',
                f'componentid:{self.component_id} status:open title:"{stable_signature}"',
                f'componentid:{self.component_id} status:open "{stable_signature}"',
            ]
            for query in queries:
                if self.cli_binary and not self.token:
                    # Use CLI backend
                    cmd = [self.cli_binary, "search", query, "--format=json"]
                    if self.qa_mode:
                        cmd.append("--api=blade:corp-issuetracker-test-api")
                    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                    issues = json.loads(res.stdout) if res.stdout.strip() else []
                    if issues:
                        return issues[0]
                else:
                    # Use REST API backend
                    params = urllib.parse.urlencode({"query": query})
                    url = f"{self.base_url}?{params}"
                    data = self._call_rest_api(url, method="GET")
                    issues = data.get("issues", [])
                    if issues:
                        return issues[0]
            return None

        return self._execute_with_retry(_search)

    def get_issue_status(self, issue_id: int) -> str:
        """Fetch the current status of an issue."""

        def _get_status() -> str:
            if self.cli_binary and not self.token:
                cmd = [self.cli_binary, "view", str(issue_id), "--format=json"]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                data = json.loads(res.stdout) if res.stdout.strip() else {}
                return data.get("issueState", {}).get("status") or data.get("status", "UNKNOWN")
            else:
                url = f"{self.base_url}/{issue_id}"
                data = self._call_rest_api(url, method="GET")
                return data.get("issueState", {}).get("status") or data.get("status", "UNKNOWN")

        return self._execute_with_retry(_get_status)

    def create_issue(self, stable_signature: str, description: str) -> int:
        """Create a new Buganizer issue in the target component."""
        clean_desc = self._truncate_payload(description)
        title = f"[Crash] {stable_signature}"

        def _create() -> int:
            if self.cli_binary and not self.token:
                cmd = [
                    self.cli_binary,
                    "new",
                    f"--title={title}",
                    f"--description={clean_desc}",
                    f"--component={self.component_id}",
                    "--status=ASSIGNED",
                    f"--tags={TAG_ANALYZED}",
                    "--format=json",
                ]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                data = json.loads(res.stdout)
                return int(data.get("issueId", data.get("id", 0)))
            else:
                payload = {
                    "issueState": {
                        "title": title,
                        "componentId": self.component_id,
                        "status": "ASSIGNED",
                    },
                    "description": clean_desc,
                    "addTags": [TAG_ANALYZED],
                }
                data = self._call_rest_api(
                    self.base_url, method="POST", payload=payload
                )
                return int(data.get("issueId", data.get("id", 0)))

        return self._execute_with_retry(_create)

    def update_issue_comment(
        self, issue_id: int, comment: str, tags: Optional[List[str]] = None
    ) -> bool:
        """Add a comment and optional tags to an existing issue."""
        clean_comment = self._truncate_payload(comment)
        add_tags = tags or [TAG_ANALYZED]

        def _update() -> bool:
            if self.cli_binary and not self.token:
                cmd = [
                    self.cli_binary,
                    "modify",
                    str(issue_id),
                    f"--comment={clean_comment}",
                    f"--add_tags={','.join(add_tags)}",
                    "--format=json",
                ]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                subprocess.run(cmd, capture_output=True, text=True, check=True)
                return True
            else:
                url = f"{self.base_url}/{issue_id}:modify"
                payload = {
                    "issueComment": {"comment": clean_comment},
                    "addTags": add_tags,
                }
                self._call_rest_api(url, method="POST", payload=payload)
                return True

        return self._execute_with_retry(_update)

    def reopen_issue_as_regression(self, issue_id: int, comment: str) -> bool:
        """Reopen a closed issue as a regression."""
        clean_comment = self._truncate_payload(comment)

        def _reopen() -> bool:
            if self.cli_binary and not self.token:
                cmd = [
                    self.cli_binary,
                    "modify",
                    str(issue_id),
                    "--status=ASSIGNED",
                    f"--comment={clean_comment}",
                    f"--add_tags={TAG_ANALYZED}",
                    "--format=json",
                ]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                subprocess.run(cmd, capture_output=True, text=True, check=True)
                return True
            else:
                url = f"{self.base_url}/{issue_id}:modify"
                payload = {
                    "issueState": {"status": "ASSIGNED"},
                    "issueComment": {"comment": clean_comment},
                    "addTags": [TAG_ANALYZED],
                }
                self._call_rest_api(url, method="POST", payload=payload)
                return True

        return self._execute_with_retry(_reopen)

    def get_issue_comments(self, issue_id: int) -> List[str]:
        """Fetch existing comments from an issue to enrich engineering context."""

        def _fetch() -> List[str]:
            if self.cli_binary and not self.token:
                cmd = [self.cli_binary, "comments", str(issue_id), "--format=json"]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                comments_data = json.loads(res.stdout) if res.stdout.strip() else []
                return [c.get("comment", "") for c in comments_data if c.get("comment")]
            else:
                url = f"{self.base_url}/{issue_id}/comments"
                data = self._call_rest_api(url, method="GET")
                comments_data = data.get("issueComments", [])
                return [c.get("comment", "") for c in comments_data if c.get("comment")]

        return self._execute_with_retry(_fetch)
