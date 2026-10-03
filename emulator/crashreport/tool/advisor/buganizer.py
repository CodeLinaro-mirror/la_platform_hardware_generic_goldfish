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
import re
import shutil
import subprocess
import tempfile
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


_GENERIC_ABORT_SIGNATURES = re.compile(
    r"(?:FailWithoutStackTrace|LogMessage::Flush|\babort\b|__pthread_kill|\braise\b|__assert_fail|_wassert|std::terminate|std::__1::__throw|LogMessage::~LogMessage)",
    re.IGNORECASE,
)


def strip_trailing_call_params(symbol: str) -> str:
    """Strips trailing function call parameters from the end of a symbol while preserving templates and operator()."""
    s = symbol.strip()
    if not s.endswith(")"):
        return s
    depth = 0
    for i in range(len(s) - 1, -1, -1):
        if s[i] == ")":
            depth += 1
        elif s[i] == "(":
            depth -= 1
            if depth == 0:
                if i >= 8 and s[i - 8 : i] == "operator":
                    return s
                prefix = s[:i].strip()
                return prefix if prefix else s
    return s


def split_top_level_namespaces(symbol: str) -> List[str]:
    """Splits a C++ symbol on '::' only outside template angle brackets <...>."""
    parts: List[str] = []
    current: List[str] = []
    depth = 0
    i = 0
    n = len(symbol)
    while i < n:
        c = symbol[i]
        if c == "<":
            depth += 1
            current.append(c)
            i += 1
        elif c == ">":
            if depth > 0:
                depth -= 1
            current.append(c)
            i += 1
        elif depth == 0 and symbol[i : i + 2] == "::":
            part = "".join(current).strip()
            if part and part != "(anonymous namespace)":
                parts.append(part)
            current = []
            i += 2
        else:
            current.append(c)
            i += 1
    last_part = "".join(current).strip()
    if last_part and last_part != "(anonymous namespace)":
        parts.append(last_part)
    return parts


def derive_crash_title(
    stable_signature: str,
    description: str = "",
    title: Optional[str] = None,
) -> str:
    """Derives an informative Buganizer issue title for a crash.

    1. If explicit `title` is passed, formats and returns it.
    2. Otherwise, scans `description` for '# Title: ...', '# Issue Title: ...',
       frontmatter 'title: ...', or a leading '# [Crash] ...' line.
    3. If `stable_signature` is a generic abort frame (e.g. FailWithoutStackTrace,
       abort, __pthread_kill), attempts to extract the first domain frame from
       stack frames present in `description`.
    4. Defaults to `f"[Crash] {stable_signature}"`.
    """
    if title and title.strip():
        t = title.strip()
        if not t.startswith("[Crash]"):
            t = f"[Crash] {t}"
        return t[:250]

    if description:
        m = re.search(
            r"^#\s*(?:Title|Issue Title):\s*(?:\"([^\n\"]+)\"|'([^\n']+)'|([^\n]+))$",
            description,
            re.MULTILINE | re.IGNORECASE,
        )
        if not m:
            m = re.search(
                r"^title:\s*(?:\"([^\n\"]+)\"|'([^\n']+)'|([^\n\r]+))",
                description,
                re.MULTILINE | re.IGNORECASE,
            )
        if not m:
            m = re.search(r"^#\s*(\[Crash\][^\n]+)$", description, re.MULTILINE)
        if m:
            t = (m.group(1) or m.group(2) or m.group(3) or "").strip()
            if t:
                if not t.startswith("[Crash]"):
                    t = f"[Crash] {t}"
                return t[:250]

    # If stable_signature is generic boilerplate, try finding the first domain frame in the description
    if _GENERIC_ABORT_SIGNATURES.search(stable_signature) and description:
        frame_matches = re.findall(
            r"#(?:[0-9]+):\s+([^\n\r]+)",
            description,
        )
        for raw_frame in frame_matches:
            frame_line = re.sub(r"\s*\[[^\]]+\]\s*$", "", raw_frame.strip())
            # Explicitly ignore raw hexadecimal memory addresses (e.g. #1: 0x7f83a12b4000)
            if frame_line.lower().startswith("0x") or not re.search(r"[a-zA-Z_]", frame_line):
                continue
            if not _GENERIC_ABORT_SIGNATURES.search(frame_line) and not frame_line.startswith(
                ("std::", "absl::", "__")
            ):
                short_symbol = strip_trailing_call_params(frame_line)
                parts = split_top_level_namespaces(short_symbol)
                if len(parts) >= 2:
                    short_symbol = "::".join(parts[-2:])
                elif parts:
                    short_symbol = parts[0]
                return f"[Crash] {short_symbol} ({stable_signature})"[:250]

    return f"[Crash] {stable_signature}"[:250]


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
        """Resolve base_cli, buganizer, or issues CLI binary if available (GLinux)."""
        for bin_name in ["buganizer", "base_cli"]:
            path = shutil.which(bin_name)
            if path:
                return path
        return shutil.which("issues") or shutil.which(
            "issues", path="/google/bin/releases/issues-cli"
        )

    def _is_issues_cli(self) -> bool:
        """Return True if the resolved CLI binary is /google/bin/releases/issues-cli/issues."""
        return bool(self.cli_binary and os.path.basename(self.cli_binary) == "issues")

    @staticmethod
    def _parse_issues_cli_output(raw: str) -> List[Dict[str, Any]]:
        """Parse human-readable output from `issues readonly search --verbose` or `get-issue-metadata`."""
        clean = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", raw)
        results: List[Dict[str, Any]] = []
        current: Dict[str, Any] = {}
        for line in clean.splitlines():
            s = line.strip()
            if (
                not s
                or s.startswith("Showing first ")
                or s.startswith("Next page token:")
            ):
                continue
            if s == "---":
                if current.get("issueId"):
                    results.append(current)
                current = {}
                continue
            m = re.match(r"^([A-Za-z ]+):\s*(.+)$", s)
            if not m:
                continue
            key, val = m.group(1).strip().lower(), m.group(2).strip()
            if key in ("issue id", "id"):
                current["issueId"] = val
                current["id"] = val
            elif key == "title":
                current["title"] = val
                current.setdefault("issueState", {})["title"] = val
            elif key == "status":
                status_tok = val.split()[0].upper()
                current["status"] = status_tok
                current.setdefault("issueState", {})["status"] = status_tok
            elif key in ("assignee", "escalation owner"):
                current.setdefault("issueState", {})["assignee"] = {
                    "emailAddress": val
                }
            elif key == "component id":
                try:
                    current.setdefault("issueState", {})["componentId"] = int(val)
                except ValueError:
                    pass
        if current.get("issueId"):
            results.append(current)
        return results

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
                if e.code == 403 and self.cli_binary and self.token:
                    logging.debug(
                        "REST API returned HTTP 403; falling back to local CLI binary %s",
                        self.cli_binary,
                    )
                    self.token = None
                    continue
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

    def _cli_search(self, query: str) -> List[Dict[str, Any]]:
        """Execute a search query using either issues CLI or buganizer/base_cli."""
        if self._is_issues_cli():
            cmd = [
                self.cli_binary,
                "readonly",
                "search",
                f"--query={query}",
                "--verbose",
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, check=True)
            return self._parse_issues_cli_output(res.stdout)
        cmd = [self.cli_binary, "search", query, "--format=json"]
        if self.qa_mode:
            cmd.append("--api=blade:corp-issuetracker-test-api")
        res = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return json.loads(res.stdout) if res.stdout.strip() else []

    def search_issue_by_signature(
        self, stable_signature: str
    ) -> Optional[Dict[str, Any]]:
        """Search Buganizer for an existing issue matching the stableSignature in the component."""

        def _search() -> Optional[Dict[str, Any]]:
            sig_variants = [stable_signature]
            m_hash = re.match(r"^(.+?)[\s\-][0-9a-fA-F]{7,8}$", stable_signature.strip())
            if m_hash:
                base_sig = m_hash.group(1).strip()
                if base_sig and base_sig not in sig_variants:
                    sig_variants.append(base_sig)

            queries: List[str] = []
            for sig in sig_variants:
                queries.extend(
                    [
                        f'componentid:{self.component_id} title:"{sig}"',
                        f'componentid:{self.component_id} "{sig}"',
                        f'componentid:{self.component_id} status:open title:"{sig}"',
                        f'componentid:{self.component_id} status:open "{sig}"',
                    ]
                )
            for query in queries:
                if self.cli_binary and not self.token:
                    issues = self._cli_search(query)
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

    def get_issue(self, issue_id: int) -> Dict[str, Any]:
        """Fetch issue details by issue ID."""

        def _get() -> Dict[str, Any]:
            if self.cli_binary and not self.token:
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "readonly",
                        "search",
                        f"--query=id:{issue_id}",
                        "--verbose",
                    ]
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=True
                    )
                    parsed = self._parse_issues_cli_output(res.stdout)
                    if parsed:
                        return parsed[0]
                    cmd = [
                        self.cli_binary,
                        "readonly",
                        "get-issue-metadata",
                        str(issue_id),
                    ]
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=True
                    )
                    parsed = self._parse_issues_cli_output(res.stdout)
                    return parsed[0] if parsed else {}
                else:
                    cmd = [self.cli_binary, "view", str(issue_id), "--format=json"]
                    if self.qa_mode:
                        cmd.append("--api=blade:corp-issuetracker-test-api")
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=True
                    )
                    return json.loads(res.stdout) if res.stdout.strip() else {}
            else:
                url = f"{self.base_url}/{issue_id}"
                return self._call_rest_api(url, method="GET")

        return self._execute_with_retry(_get)

    def get_issue_status(self, issue_id: int) -> str:
        """Fetch the current status of an issue."""
        data = self.get_issue(issue_id)
        return (data.get("issueState") or {}).get("status") or data.get("status", "UNKNOWN")

    def get_issue_title(self, issue_id: int) -> str:
        """Fetch the current title of an issue."""
        data = self.get_issue(issue_id)
        return (data.get("issueState") or {}).get("title") or data.get("title", "")

    def update_issue_title(self, issue_id: int, new_title: str) -> bool:
        """Update the title of an existing Buganizer issue."""

        def _update() -> bool:
            if self.cli_binary and not self.token:
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "mutate",
                        "update",
                        "title",
                        f"--issue_id={issue_id}",
                        f"--title={new_title}",
                    ]
                    subprocess.run(
                        cmd, capture_output=True, text=True, check=True
                    )
                    return True
                cmd = [
                    self.cli_binary,
                    "modify",
                    str(issue_id),
                    f"--title={new_title}",
                    "--format=json",
                ]
                if self.qa_mode:
                    cmd.append("--api=blade:corp-issuetracker-test-api")
                subprocess.run(
                    cmd, capture_output=True, text=True, check=True
                )
                return True
            else:
                url = f"{self.base_url}/{issue_id}:modify"
                payload = {
                    "issueState": {"title": new_title},
                }
                self._call_rest_api(url, method="POST", payload=payload)
                return True

        return self._execute_with_retry(_update)

    def create_issue(
        self,
        stable_signature: str,
        description: str,
        title: Optional[str] = None,
    ) -> int:
        """Create a new Buganizer issue in the target component."""
        clean_desc = self._truncate_payload(description)
        resolved_title = derive_crash_title(stable_signature, clean_desc, title=title)

        def _create() -> int:
            if self.cli_binary and not self.token:
                if self._is_issues_cli():
                    with tempfile.NamedTemporaryFile(
                        "w", encoding="utf-8", delete=False
                    ) as tf:
                        tf.write(clean_desc)
                        tf_path = tf.name
                    try:
                        cmd = [
                            self.cli_binary,
                            "mutate",
                            "create",
                            f"--component_id={self.component_id}",
                            f"--title={resolved_title}",
                            f"--description_file={tf_path}",
                            "--priority=P2",
                            "--severity=S2",
                        ]
                        res = subprocess.run(
                            cmd, capture_output=True, text=True, check=True
                        )
                        m = re.search(r"b/(\d+)", res.stdout) or re.search(
                            r"(\d{6,})", res.stdout
                        )
                        return int(m.group(1)) if m else 0
                    finally:
                        try:
                            os.unlink(tf_path)
                        except OSError:
                            pass
                cmd = [
                    self.cli_binary,
                    "new",
                    f"--title={resolved_title}",
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
                        "title": resolved_title,
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
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "mutate",
                        "comment",
                        f"--issue_id={issue_id}",
                        f"--comment={clean_comment}",
                    ]
                    subprocess.run(cmd, capture_output=True, text=True, check=True)
                    return True
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
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "mutate",
                        "comment",
                        f"--issue_id={issue_id}",
                        f"--comment={clean_comment}",
                    ]
                    subprocess.run(cmd, capture_output=True, text=True, check=True)
                    return True
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
                if self._is_issues_cli():
                    cmd = [self.cli_binary, "readonly", "render", str(issue_id)]
                    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
                    return [res.stdout] if res.stdout.strip() else []
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
