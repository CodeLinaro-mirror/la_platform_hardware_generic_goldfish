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

GENERAL_CRASHES_HOTLIST_NAME = "qemu-next-crashes"
VERSION_CRASHES_HOTLIST_PREFIX = "qemu-next-crashes-version-"


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


def clean_version_string(ver: Optional[Any]) -> str:
    """Extracts clean release milestone version (e.g. '37.1.3') from version strings."""
    if ver is None:
        return ""
    ver_str = str(ver).strip()
    if not ver_str:
        return ""
    if "-" in ver_str:
        ver_str = ver_str.split("-", 1)[0].strip()
    return ver_str


def has_new_crash_information(
    existing_issue_data: Dict[str, Any],
    existing_comments: List[str],
    new_crash: Dict[str, Any],
) -> Tuple[bool, List[str]]:
    """Evaluates whether a new crash report provides novel information over an existing bug.

    Checks for:
    1. Potential regression on a CLOSED/FIXED/VERIFIED bug.
    2. New host operating system platform (e.g., first occurrence on macOS or Windows).
    3. Novel error messages, abort reasons, or crash annotations/breadcrumbs.
    4. Divergent top faulting location (source file / function).

    Args:
        existing_issue_data: Buganizer issue dict (with issueState, title, description).
        existing_comments: List of previous comment/render strings from the issue.
        new_crash: Dictionary of the new crash report (with os, versions, build_ids,
                   crash_location, custom_keys, internal_msg, list_annotations, etc.).

    Returns:
        Tuple of (has_new_info: bool, reasons: List[str]).
    """
    reasons: List[str] = []

    # Aggregate existing text for keyword / containment searches
    desc = str(existing_issue_data.get("description") or "")
    state = existing_issue_data.get("issueState") or {}
    title = str(state.get("title") or existing_issue_data.get("title") or "")
    safe_comments = [str(c or "") for c in existing_comments]
    all_text = " ".join(" ".join([title, desc] + safe_comments).lower().split())

    # 1. Check for regression on closed / fixed bug
    status = str(
        state.get("status")
        or existing_issue_data.get("status")
        or ""
    ).upper()
    if status in ("FIXED", "VERIFIED", "OBSOLETE", "CLOSED"):
        raw_bids = new_crash.get("build_ids") or []
        if not isinstance(raw_bids, (list, tuple, set)):
            raw_bids = [raw_bids]
        builds = [str(b).strip() for b in raw_bids if b is not None and str(b).strip()]
        build_str = f" on Build ID {builds[0]}" if builds else ""
        reg_msg = (
            f"Potential regression: Crash reproduced after bug was marked {status}{build_str}"
        )
        if reg_msg.lower() not in all_text:
            reasons.append(reg_msg)

    # 2. Check for new OS platform
    mac_re = re.compile(r"\b(?:mac\s*os|macos|macosx|darwin|osx)\b", re.IGNORECASE)
    win_re = re.compile(r"\b(?:windows|win32|win64|win\s*10|win\s*11)\b", re.IGNORECASE)
    linux_re = re.compile(r"\b(?:linux)\b", re.IGNORECASE)

    new_os_list = new_crash.get("os") or []
    if not isinstance(new_os_list, (list, tuple, set)):
        new_os_list = [new_os_list]
    for os_item in new_os_list:
        if not os_item:
            continue
        os_str = str(os_item).strip()
        if mac_re.search(os_str):
            if not mac_re.search(all_text):
                reasons.append(f"New host platform: First occurrence observed on macOS ({os_str})")
        elif win_re.search(os_str):
            if not win_re.search(all_text):
                reasons.append(f"New host platform: First occurrence observed on Windows ({os_str})")
        elif linux_re.search(os_str):
            if not linux_re.search(all_text):
                reasons.append(f"New host platform: First occurrence observed on Linux ({os_str})")

    # 3. Check for novel error message or breadcrumbs
    custom_keys = new_crash.get("custom_keys") or {}
    internal_msg = (
        custom_keys.get("internal-msg")
        or new_crash.get("internal_msg")
        or new_crash.get("internal-msg")
        or ""
    )
    if internal_msg:
        compact_msg = " ".join(str(internal_msg).split())
        if len(compact_msg) > 5:
            msg_snippet = compact_msg[:200]
            if msg_snippet.lower() not in all_text:
                reasons.append(f"New internal error message: '{msg_snippet}'")

    list_annos = (
        custom_keys.get("list_annotations")
        or new_crash.get("list_annotations")
        or ""
    )
    if list_annos:
        clean_anno = " ".join(str(list_annos).split())
        if len(clean_anno) > 3:
            anno_snippet = clean_anno[:200]
            if anno_snippet.lower() not in all_text:
                reasons.append(f"New crash annotation breadcrumb: '{anno_snippet}'")

    # 4. Check for divergent top faulting location
    loc = new_crash.get("crash_location") or ""
    if loc and loc != "N/A":
        loc_clean = re.sub(
            r"^(?:Guest|Host)[\s:(]+", "", str(loc).strip(), flags=re.IGNORECASE
        )
        if loc_clean and loc_clean != "N/A":
            m = re.match(r"^([A-Za-z0-9_:]+)", loc_clean)
            if m:
                func = m.group(1)
                if (
                    len(func) > 4
                    and not re.match(r"^0x[0-9a-fA-F]+$", func)
                    and func.lower() not in all_text
                    and not _GENERIC_ABORT_SIGNATURES.search(func)
                ):
                    reasons.append(f"Different faulting function: {func} in {loc}")

    return bool(reasons), reasons


def format_new_crash_info_comment(
    report_id: str,
    reasons: List[str],
    new_crash: Dict[str, Any],
) -> str:
    """Formats a concise Buganizer comment summarizing newly discovered crash information."""
    rep_link = f"http://go/crash/{report_id}" if report_id else "go/crash"
    lines = [
        f"### Additional Crash Report: {rep_link}",
        "",
        "**New Information Discovered:**",
    ]
    for r in reasons:
        lines.append(f"- {r}")
    lines.append("")

    raw_ver = new_crash.get("versions") or []
    if not isinstance(raw_ver, (list, tuple, set)):
        raw_ver = [raw_ver]
    versions = ", ".join(str(x) for x in raw_ver if x is not None and str(x).strip()) or "N/A"

    raw_bids = new_crash.get("build_ids") or []
    if not isinstance(raw_bids, (list, tuple, set)):
        raw_bids = [raw_bids]
    build_ids = ", ".join(str(x) for x in raw_bids if x is not None and str(x).strip()) or "N/A"

    raw_os = new_crash.get("os") or []
    if not isinstance(raw_os, (list, tuple, set)):
        raw_os = [raw_os]
    os_list = ", ".join(str(x) for x in raw_os if x is not None and str(x).strip()) or "N/A"

    loc = new_crash.get("crash_location") or "N/A"

    lines.extend(
        [
            f"- **Host OS:** `{os_list}`",
            f"- **Build IDs / Version:** `{build_ids}` (`{versions}`)",
            f"- **Crash Location:** `{loc}`",
        ]
    )
    return "\n".join(lines)


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
        self._hotlist_cache: Dict[str, int] = {}

    clean_version_string = staticmethod(clean_version_string)
    has_new_crash_information = staticmethod(has_new_crash_information)
    format_new_crash_info_comment = staticmethod(format_new_crash_info_comment)

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

    @staticmethod
    def _parse_hotlists_cli_output(raw: str) -> List[Dict[str, Any]]:
        """Parse human-readable output from `issues readonly list-hotlists`."""
        clean = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", raw)
        results: List[Dict[str, Any]] = []
        current: Dict[str, Any] = {}
        for line in clean.splitlines():
            s = line.strip()
            if not s:
                if current.get("id"):
                    results.append(current)
                    current = {}
                continue
            m = re.match(r"^Hotlist\s+([A-Za-z]+):\s*(.*)$", s, re.IGNORECASE)
            if m:
                key, val = m.group(1).lower(), m.group(2).strip()
                if key == "id":
                    if current.get("id"):
                        results.append(current)
                        current = {}
                    current["id"] = int(val) if val.isdigit() else val
                elif key in ("name", "title"):
                    current["name"] = val
                elif key == "description":
                    current["description"] = val
        if current.get("id"):
            results.append(current)
        return results

    @staticmethod
    def _parse_created_hotlist_id(raw: str) -> int:
        """Extract created hotlist ID from CLI command output."""
        clean = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", raw)
        m = re.search(
            r"(?:created(?:\s+hotlist)?(?:\s+id)?|hotlist(?:\s+id)?)\s*[:=]?\s*(\d+)",
            clean,
            re.IGNORECASE,
        )
        if m:
            return int(m.group(1))
        return 0

    def get_or_create_hotlist(self, title: str, description: str = "") -> int:
        """Find an existing hotlist by exact title or create it if not found."""
        title_clean = title.strip()
        if not title_clean:
            return 0
        if title_clean in self._hotlist_cache:
            return self._hotlist_cache[title_clean]

        def _get_or_create() -> int:
            if self.cli_binary and not self.token:
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "readonly",
                        "list-hotlists",
                        f"--query={title_clean}",
                    ]
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=False
                    )
                    if res.returncode == 0 and res.stdout.strip():
                        hotlists = self._parse_hotlists_cli_output(res.stdout)
                        for hl in hotlists:
                            if str(hl.get("name") or "").strip().lower() == title_clean.lower():
                                raw_id = str(hl.get("id") or "").strip()
                                if raw_id.isdigit():
                                    hl_id = int(raw_id)
                                    if hl_id > 0:
                                        self._hotlist_cache[title_clean] = hl_id
                                        return hl_id

                    desc = description or f"Crash tracking hotlist for {title_clean}"
                    create_cmd = [
                        self.cli_binary,
                        "mutate",
                        "create-hotlist",
                        f"--title={title_clean}",
                        f"--description={desc}",
                    ]
                    create_res = subprocess.run(
                        create_cmd, capture_output=True, text=True, check=False
                    )
                    if create_res.returncode == 0 and create_res.stdout.strip():
                        hl_id = self._parse_created_hotlist_id(create_res.stdout)
                        if hl_id > 0:
                            self._hotlist_cache[title_clean] = hl_id
                            return hl_id

                    res2 = subprocess.run(
                        cmd, capture_output=True, text=True, check=False
                    )
                    if res2.returncode == 0 and res2.stdout.strip():
                        for hl in self._parse_hotlists_cli_output(res2.stdout):
                            if str(hl.get("name") or "").strip().lower() == title_clean.lower():
                                raw_id = str(hl.get("id") or "").strip()
                                if raw_id.isdigit():
                                    hl_id = int(raw_id)
                                    if hl_id > 0:
                                        self._hotlist_cache[title_clean] = hl_id
                                        return hl_id
                    return 0
                else:
                    cmd = [self.cli_binary, "hotlist", "search", title_clean, "--format=json"]
                    if self.qa_mode:
                        cmd.append("--api=blade:corp-issuetracker-test-api")
                    res = subprocess.run(cmd, capture_output=True, text=True, check=False)
                    if res.returncode == 0 and res.stdout.strip():
                        try:
                            hotlists = json.loads(res.stdout)
                            if isinstance(hotlists, dict):
                                hotlists = hotlists.get("hotlists", [])
                            for hl in hotlists:
                                if isinstance(hl, dict) and str(hl.get("name") or "").strip().lower() == title_clean.lower():
                                    raw_id = str(hl.get("id") or hl.get("hotlistId") or "").strip()
                                    if raw_id.isdigit():
                                        hl_id = int(raw_id)
                                        if hl_id > 0:
                                            self._hotlist_cache[title_clean] = hl_id
                                            return hl_id
                        except Exception:
                            pass

                    desc = description or f"Crash tracking hotlist for {title_clean}"
                    create_cmd = [
                        self.cli_binary,
                        "hotlist",
                        "create",
                        title_clean,
                        f"--description={desc}",
                        "--format=json",
                    ]
                    if self.qa_mode:
                        create_cmd.append("--api=blade:corp-issuetracker-test-api")
                    create_res = subprocess.run(
                        create_cmd, capture_output=True, text=True, check=False
                    )
                    if create_res.returncode == 0 and create_res.stdout.strip():
                        try:
                            created_data = json.loads(create_res.stdout)
                            if isinstance(created_data, dict):
                                raw_id = str(
                                    created_data.get("id")
                                    or created_data.get("hotlistId")
                                    or ""
                                ).strip()
                                hl_id = int(raw_id) if raw_id.isdigit() else 0
                                if hl_id > 0:
                                    self._hotlist_cache[title_clean] = hl_id
                                    return hl_id
                        except Exception:
                            hl_id = self._parse_created_hotlist_id(create_res.stdout)
                            if hl_id > 0:
                                self._hotlist_cache[title_clean] = hl_id
                                return hl_id

                    issues_cli = shutil.which("issues") or shutil.which(
                        "issues", path="/google/bin/releases/issues-cli"
                    )
                    if issues_cli and issues_cli != self.cli_binary:
                        create_cmd = [
                            issues_cli,
                            "mutate",
                            "create-hotlist",
                            f"--title={title_clean}",
                            f"--description={desc}",
                        ]
                        create_res = subprocess.run(
                            create_cmd, capture_output=True, text=True, check=False
                        )
                        if create_res.returncode == 0 and create_res.stdout.strip():
                            hl_id = self._parse_created_hotlist_id(create_res.stdout)
                            if hl_id > 0:
                                self._hotlist_cache[title_clean] = hl_id
                                return hl_id

                    return 0
            else:
                base_hotlists_url = self.base_url.rsplit("/issues", 1)[0] + "/hotlists"
                params = urllib.parse.urlencode({"query": f'name:"{title_clean}"'})
                url = f"{base_hotlists_url}?{params}"
                try:
                    data = self._call_rest_api(url, method="GET")
                    hotlists = (data.get("hotlists") or []) if isinstance(data, dict) else []
                    for hl in hotlists:
                        if isinstance(hl, dict) and str(hl.get("name") or "").strip().lower() == title_clean.lower():
                            raw_id = str(hl.get("hotlistId") or hl.get("id") or "").strip()
                            if raw_id.isdigit():
                                hl_id = int(raw_id)
                                if hl_id > 0:
                                    self._hotlist_cache[title_clean] = hl_id
                                    return hl_id
                except urllib.error.HTTPError as e:
                    if e.code == 404:
                        pass
                    else:
                        raise

                desc = description or f"Crash tracking hotlist for {title_clean}"
                payload = {
                    "hotlist": {
                        "name": title_clean,
                        "description": desc,
                    }
                }
                data = self._call_rest_api(
                    base_hotlists_url, method="POST", payload=payload
                )
                raw_id = str(
                    (data.get("hotlistId") or data.get("id") or "")
                    if isinstance(data, dict)
                    else ""
                ).strip()
                hl_id = int(raw_id) if raw_id.isdigit() else 0
                if hl_id > 0:
                    self._hotlist_cache[title_clean] = hl_id
                    return hl_id
                return 0

        return self._execute_with_retry(_get_or_create)

    def add_issue_to_hotlist(self, issue_id: int, hotlist_id: int) -> bool:
        """Add an issue to a hotlist."""
        if not issue_id or not hotlist_id:
            return False

        def _add() -> bool:
            if self.cli_binary and not self.token:
                if self._is_issues_cli():
                    cmd = [
                        self.cli_binary,
                        "mutate",
                        "update",
                        "add-hotlist",
                        f"--issue_id={issue_id}",
                        f"--hotlist_id={hotlist_id}",
                    ]
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=False
                    )
                    return res.returncode == 0
                else:
                    cmd = [
                        self.cli_binary,
                        "modify",
                        str(issue_id),
                        f"--add_hotlists={hotlist_id}",
                        "--format=json",
                    ]
                    if self.qa_mode:
                        cmd.append("--api=blade:corp-issuetracker-test-api")
                    res = subprocess.run(
                        cmd, capture_output=True, text=True, check=False
                    )
                    return res.returncode == 0
            else:
                base_hotlists_url = self.base_url.rsplit("/issues", 1)[0] + "/hotlists"
                url = f"{base_hotlists_url}/{hotlist_id}/entries"
                payload = {
                    "hotlistEntry": {
                        "issueId": str(issue_id),
                    }
                }
                try:
                    self._call_rest_api(url, method="POST", payload=payload)
                    return True
                except urllib.error.HTTPError as e:
                    if e.code == 409:
                        return True
                    raise

        return self._execute_with_retry(_add)

    def ensure_crash_hotlists(
        self, issue_id: int, version: Optional[str] = None
    ) -> List[int]:
        """Ensures an issue is tagged with general and version-specific qemu-next crash hotlists.

        Adds issue to:
        1. General hotlist: 'qemu-next-crashes'
        2. Version-specific hotlist: 'qemu-next-crashes-version-<clean_version>' (if version provided)

        Returns:
            List of hotlist IDs the issue was added to.
        """
        if not issue_id:
            return []
        hotlist_ids: List[int] = []
        try:
            gen_id = self.get_or_create_hotlist(
                GENERAL_CRASHES_HOTLIST_NAME,
                description="General tracking hotlist for all qemu-next production crashes",
            )
            if gen_id:
                if self.add_issue_to_hotlist(issue_id, gen_id):
                    hotlist_ids.append(gen_id)

            clean_ver = clean_version_string(version)
            if clean_ver:
                ver_name = f"{VERSION_CRASHES_HOTLIST_PREFIX}{clean_ver}"
                ver_id = self.get_or_create_hotlist(
                    ver_name,
                    description=f"Tracking hotlist for qemu-next crashes observed in version {clean_ver}",
                )
                if ver_id:
                    if self.add_issue_to_hotlist(issue_id, ver_id):
                        hotlist_ids.append(ver_id)
        except Exception as e:
            logging.warning("Failed to attach hotlists to issue b/%d: %s", issue_id, e)

        return hotlist_ids
