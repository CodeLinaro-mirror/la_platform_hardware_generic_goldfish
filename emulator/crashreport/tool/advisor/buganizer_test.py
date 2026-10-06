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

"""Unit tests for BuganizerClient covering REST and CLI backends, exponential backoff, and truncation."""

import json
import subprocess
import unittest
import urllib.error
from unittest.mock import MagicMock, patch

from buganizer import (
    EMULATOR_COMPONENT_ID,
    GENERAL_CRASHES_HOTLIST_NAME,
    PROD_REST_ENDPOINT,
    QA_REST_ENDPOINT,
    TAG_ANALYZED,
    TAG_DISPATCHED,
    VERSION_CRASHES_HOTLIST_PREFIX,
    BuganizerAuthError,
    BuganizerClient,
    BuganizerError,
    clean_version_string,
    derive_crash_title,
    format_new_crash_info_comment,
    has_new_crash_information,
)


class TestBuganizerClient(unittest.TestCase):
    """Hermetic test suite for BuganizerClient."""

    def setUp(self) -> None:
        self.stable_sig = "CrashReporterImpl::Die-1234"
        self.token = "fake_oauth2_token"

    @patch("buganizer.shutil.which")
    def test_init_cli_resolution(self, mock_which: MagicMock) -> None:
        """Test correct resolution of CLI binary backend."""
        mock_which.side_effect = lambda name: (
            "/usr/bin/buganizer" if name == "buganizer" else None
        )
        client = BuganizerClient(token=self.token)
        self.assertEqual(client.cli_binary, "/usr/bin/buganizer")
        self.assertEqual(client.base_url, PROD_REST_ENDPOINT)

    def test_init_qa_mode_endpoint(self) -> None:
        """Test QA mode configures the correct b-qa endpoint."""
        client = BuganizerClient(token=self.token, qa_mode=True)
        self.assertEqual(client.base_url, QA_REST_ENDPOINT)

    @patch.dict("os.environ", {}, clear=True)
    def test_missing_token_raises_auth_error(self) -> None:
        """Test BuganizerClient raises BuganizerAuthError when token and CLI are missing."""
        with patch("buganizer.shutil.which", return_value=None):
            client = BuganizerClient(token=None)
            with self.assertRaises(BuganizerAuthError):
                client._get_auth_token()

    @patch.dict("os.environ", {"BUGANIZER_TOKEN": "env_token"}, clear=True)
    def test_env_token_resolution(self) -> None:
        """Test BuganizerClient correctly resolves token from BUGANIZER_TOKEN env var."""
        client = BuganizerClient(token=None)
        self.assertEqual(client._get_auth_token(), "env_token")

    def test_truncate_payload(self) -> None:
        """Test payload truncation enforcement for Buganizer comment limits."""
        client = BuganizerClient(token=self.token)
        short_text = "Hello Buganizer"
        self.assertEqual(client._truncate_payload(short_text, max_len=100), short_text)

        long_text = "A" * 200
        truncated = client._truncate_payload(long_text, max_len=100)
        self.assertEqual(len(truncated), 100)
        self.assertIn("Truncated for Buganizer size limits", truncated)

    @patch("buganizer.urllib.request.urlopen")
    def test_search_issue_rest_backend(self, mock_urlopen: MagicMock) -> None:
        """Test search_issue_by_signature execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps(
            {"issues": [{"id": 123456, "issueState": {"status": "ASSIGNED"}}]}
        ).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        issue = client.search_issue_by_signature(self.stable_sig)

        self.assertIsNotNone(issue)
        self.assertEqual(issue["id"], 123456)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        self.assertIn(PROD_REST_ENDPOINT, req.full_url)

    @patch("buganizer.subprocess.run")
    @patch("buganizer.shutil.which")
    def test_search_issue_cli_backend(
        self, mock_which: MagicMock, mock_run: MagicMock
    ) -> None:
        """Test search_issue_by_signature execution via CLI wrapper backend."""
        mock_which.return_value = "/usr/bin/buganizer"
        mock_run.return_value = MagicMock(
            stdout=json.dumps([{"id": 654321, "status": "NEW"}])
        )

        client = BuganizerClient(token=None)  # Token None triggers CLI backend
        issue = client.search_issue_by_signature(self.stable_sig)

        self.assertIsNotNone(issue)
        self.assertEqual(issue["id"], 654321)
        mock_run.assert_called_once()
        cmd = mock_run.call_args[0][0]
        self.assertIn("/usr/bin/buganizer", cmd)
        self.assertIn("search", cmd)
        self.assertIn(f"componentid:{EMULATOR_COMPONENT_ID}", cmd[2])

    @patch("buganizer.urllib.request.urlopen")
    def test_create_issue_rest_backend(self, mock_urlopen: MagicMock) -> None:
        """Test create_issue execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps({"issueId": 987654}).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        issue_id = client.create_issue(self.stable_sig, "Crash stack trace dummy")

        self.assertEqual(issue_id, 987654)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        self.assertEqual(req.method, "POST")
        payload = json.loads(req.data.decode("utf-8"))
        self.assertEqual(payload["issueState"]["title"], f"[Crash] {self.stable_sig}")
        self.assertEqual(payload["issueState"]["componentId"], EMULATOR_COMPONENT_ID)

    @patch("buganizer.urllib.request.urlopen")
    def test_create_issue_with_custom_title(self, mock_urlopen: MagicMock) -> None:
        """Test create_issue uses explicit custom title."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps({"issueId": 112233}).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        issue_id = client.create_issue(
            self.stable_sig,
            "Crash description",
            title="[Crash][macOS] Launcher: port binding failure",
        )

        self.assertEqual(issue_id, 112233)
        req = mock_urlopen.call_args[0][0]
        payload = json.loads(req.data.decode("utf-8"))
        self.assertEqual(
            payload["issueState"]["title"],
            "[Crash][macOS] Launcher: port binding failure",
        )

    def test_derive_crash_title_prefers_explicit_title(self) -> None:
        """Test derive_crash_title respects explicit title parameter."""
        title = derive_crash_title(
            "FailWithoutStackTrace-7a37e8e9",
            description="# Title: [Crash] Something else",
            title="Launcher: port conflict",
        )
        self.assertEqual(title, "[Crash] Launcher: port conflict")

    def test_derive_crash_title_extracts_from_rca_heading(self) -> None:
        """Test derive_crash_title extracts title from '# Title:' in markdown."""
        desc = (
            "# Title: [Crash][macOS] Launcher port conflict\n"
            "### Root Cause\nPorts exhausted."
        )
        title = derive_crash_title("FailWithoutStackTrace-7a37e8e9", description=desc)
        self.assertEqual(title, "[Crash][macOS] Launcher port conflict")

    def test_derive_crash_title_generic_abort_fallback(self) -> None:
        """Test derive_crash_title skips FailWithoutStackTrace and finds domain frame."""
        desc = (
            "### Crashing Stack Trace\n"
            "#7: absl::log_internal::LogMessage::FailWithoutStackTrace()\n"
            "#8: absl::log_internal::LogMessage::Flush()\n"
            "#9: absl::log_internal::Voidify::operator&&\n"
            "#10: android::goldfish::Launcher::Launcher(LauncherConfig)\n"
        )
        title = derive_crash_title(
            "absl::lts_20260526::log_internal::LogMessage::FailWithoutStackTrace-7a37e8e9",
            description=desc,
        )
        self.assertIn("Launcher::Launcher", title)
        self.assertIn("FailWithoutStackTrace-7a37e8e9", title)
        self.assertTrue(title.startswith("[Crash]"))

    def test_derive_crash_title_abort_and_raise_word_boundaries(self) -> None:
        """Test derive_crash_title matches abort-hash and raise-hash with word boundaries."""
        desc = (
            "### Crashing Stack Trace\n"
            "#0: __pthread_kill()\n"
            "#1: raise()\n"
            "#2: abort()\n"
            "#3: android::goldfish::HostRenderer::DrawFrame()\n"
        )
        title_abort = derive_crash_title("abort-7a37e8e9", description=desc)
        self.assertIn("HostRenderer::DrawFrame", title_abort)
        self.assertIn("abort-7a37e8e9", title_abort)

        title_raise = derive_crash_title("raise-1234abcd", description=desc)
        self.assertIn("HostRenderer::DrawFrame", title_raise)
        self.assertIn("raise-1234abcd", title_raise)

    def test_derive_crash_title_quoted_with_apostrophe(self) -> None:
        """Test derive_crash_title parses double-quoted title containing apostrophes."""
        desc1 = '---\ntitle: "User\'s session crash in emulator"\n---\nSome details.'
        title1 = derive_crash_title("some_sig", description=desc1)
        self.assertEqual(title1, "[Crash] User's session crash in emulator")

        desc2 = "# Title: \"User's session crash in emulator\"\nDetails"
        title2 = derive_crash_title("some_sig", description=desc2)
        self.assertEqual(title2, "[Crash] User's session crash in emulator")

    def test_derive_crash_title_filters_hex_addresses_and_preserves_templates(self) -> None:
        """Test derive_crash_title skips hex addresses and preserves C++ templates and operator()."""
        desc = (
            "### Crashing Stack Trace\n"
            "#0: abort()\n"
            "#1: 0x7f83a12b4000 [qemu-system-x86_64]\n"
            "#2: 0x00007fff5fc24000\n"
            "#3: android::goldfish::Container<std::string>::ProcessItem(std::string)\n"
        )
        title = derive_crash_title("abort-11223344", description=desc)
        self.assertIn("Container<std::string>::ProcessItem", title)
        self.assertIn("abort-11223344", title)

        desc_op = (
            "### Crashing Stack Trace\n"
            "#0: abort()\n"
            "#1: 0x7f83a12b4000\n"
            "#2: android::goldfish::Invoker::operator()(int)\n"
        )
        title_op = derive_crash_title("abort-11223344", description=desc_op)
        self.assertIn("Invoker::operator()", title_op)

    @patch("buganizer.urllib.request.urlopen")
    def test_get_issue_status_and_title_handles_none_issue_state(
        self, mock_urlopen: MagicMock
    ) -> None:
        """Test get_issue_status and get_issue_title handle issueState: None safely."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps(
            {"issueId": 123456, "issueState": None, "status": "FIXED"}
        ).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        self.assertEqual(client.get_issue_status(123456), "FIXED")
        self.assertEqual(client.get_issue_title(123456), "")

    @patch("buganizer.urllib.request.urlopen")
    def test_update_issue_comment_rest_backend(self, mock_urlopen: MagicMock) -> None:
        """Test update_issue_comment execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps({"issueId": 123456}).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        success = client.update_issue_comment(123456, "Adding RCA summary")

        self.assertTrue(success)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        self.assertIn("123456:modify", req.full_url)
        payload = json.loads(req.data.decode("utf-8"))
        self.assertEqual(payload["issueComment"]["comment"], "Adding RCA summary")
        self.assertEqual(payload["addTags"], [TAG_ANALYZED])

    @patch("buganizer.urllib.request.urlopen")
    def test_reopen_issue_as_regression(self, mock_urlopen: MagicMock) -> None:
        """Test reopen_issue_as_regression execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps({"issueId": 123456}).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        success = client.reopen_issue_as_regression(123456, "Reopening regression")

        self.assertTrue(success)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        payload = json.loads(req.data.decode("utf-8"))
        self.assertEqual(payload["issueState"]["status"], "ASSIGNED")
        self.assertEqual(payload["issueComment"]["comment"], "Reopening regression")

    @patch("buganizer.urllib.request.urlopen")
    def test_get_issue_comments(self, mock_urlopen: MagicMock) -> None:
        """Test get_issue_comments execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps(
            {"issueComments": [{"comment": "Human tried fix A"}, {"comment": "Failed"}]}
        ).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        comments = client.get_issue_comments(123456)

        self.assertEqual(comments, ["Human tried fix A", "Failed"])
        mock_urlopen.assert_called_once()
        self.assertIn("123456/comments", mock_urlopen.call_args[0][0].full_url)

    @patch("buganizer.urllib.request.urlopen")
    def test_get_issue_title_rest_backend(self, mock_urlopen: MagicMock) -> None:
        """Test get_issue_title execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps(
            {"issueId": 123456, "issueState": {"title": "[Crash] FooBar"}}
        ).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        title = client.get_issue_title(123456)

        self.assertEqual(title, "[Crash] FooBar")
        mock_urlopen.assert_called_once()
        self.assertIn("123456", mock_urlopen.call_args[0][0].full_url)

    @patch("buganizer.urllib.request.urlopen")
    def test_update_issue_title_rest_backend(self, mock_urlopen: MagicMock) -> None:
        """Test update_issue_title execution via REST backend."""
        mock_resp = MagicMock()
        mock_resp.read.return_value = json.dumps({"issueId": 123456}).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_resp

        client = BuganizerClient(token=self.token)
        success = client.update_issue_title(123456, "[Crash][Linux] Baz: New Title")

        self.assertTrue(success)
        mock_urlopen.assert_called_once()
        req = mock_urlopen.call_args[0][0]
        self.assertIn("123456:modify", req.full_url)
        payload = json.loads(req.data.decode("utf-8"))
        self.assertEqual(payload["issueState"]["title"], "[Crash][Linux] Baz: New Title")

    @patch("buganizer.time.sleep", return_value=None)
    @patch("buganizer.urllib.request.urlopen")
    def test_exponential_backoff_retry(
        self, mock_urlopen: MagicMock, mock_sleep: MagicMock
    ) -> None:
        """Test exponential backoff retries on HTTP 429 Too Many Requests."""
        err_429 = urllib.error.HTTPError(
            "http://fake", 429, "Too Many Requests", {}, None
        )
        mock_resp = MagicMock()
        mock_resp.__enter__.return_value = mock_resp
        mock_resp.read.return_value = json.dumps({"issueId": 987654}).encode("utf-8")

        mock_urlopen.side_effect = [err_429, err_429, mock_resp]

        client = BuganizerClient(token=self.token)
        issue_id = client.create_issue(self.stable_sig, "Retrying dummy")

        self.assertEqual(issue_id, 987654)
        self.assertEqual(mock_urlopen.call_count, 3)
        self.assertEqual(mock_sleep.call_count, 2)

    @patch("buganizer.urllib.request.urlopen")
    def test_http_401_raises_buganizer_auth_error(
        self, mock_urlopen: MagicMock
    ) -> None:
        """Test HTTP 401 Unauthorized immediately raises BuganizerAuthError for idempotent resumption."""
        err_401 = urllib.error.HTTPError("http://fake", 401, "Unauthorized", {}, None)
        mock_urlopen.side_effect = err_401

        client = BuganizerClient(token=self.token)
        with self.assertRaises(BuganizerAuthError) as ctx:
            client.create_issue(self.stable_sig, "Auth error dummy")
        self.assertIn("OAuth2 token expired (HTTP 401)", str(ctx.exception))

    @patch("buganizer.subprocess.run")
    @patch("buganizer.shutil.which")
    def test_search_issue_unhashed_signature_fallback(
        self, mock_which: MagicMock, mock_run: MagicMock
    ) -> None:
        """Test search_issue_by_signature falls back to stripped signature when hash suffix returns empty."""
        mock_which.return_value = "/usr/bin/buganizer"
        empty_res = MagicMock(stdout="[]")
        match_res = MagicMock(
            stdout=json.dumps([{"id": 565638477, "status": "ASSIGNED"}])
        )
        mock_run.side_effect = [empty_res, empty_res, empty_res, empty_res, match_res]

        client = BuganizerClient(token=None)
        issue = client.search_issue_by_signature(
            "[Assert] android::qemu2::ModemSimulator::ReadOnSocket-88f45ea2"
        )
        self.assertIsNotNone(issue)
        self.assertEqual(issue["id"], 565638477)
        self.assertEqual(mock_run.call_count, 5)
        fifth_cmd = mock_run.call_args_list[4][0][0]
        self.assertIn(
            'title:"[Assert] android::qemu2::ModemSimulator::ReadOnSocket"',
            fifth_cmd[2],
        )

    @patch("buganizer.subprocess.run")
    @patch("buganizer.urllib.request.urlopen")
    @patch("buganizer.shutil.which")
    def test_http_403_falls_back_to_issues_cli(
        self,
        mock_which: MagicMock,
        mock_urlopen: MagicMock,
        mock_run: MagicMock,
    ) -> None:
        """Test HTTP 403 on REST API falls back to issues CLI when available."""
        mock_which.side_effect = lambda name, **kwargs: (
            "/google/bin/releases/issues-cli/issues" if name == "issues" else None
        )
        mock_urlopen.side_effect = urllib.error.HTTPError(
            "http://fake", 403, "Forbidden", {}, None
        )
        mock_run.return_value = MagicMock(
            stdout=(
                "Showing first 1 results:\n"
                "Issue ID: 567580358\n"
                "Title: [Crash] gfxstream::host::BackendCallbacks\n"
                "Status:NEW (P2/S2)\n"
                "Assignee: user@google.com\n"
                "Component ID: 29601\n"
                "---\n"
            )
        )

        client = BuganizerClient(token=self.token)
        issue = client.search_issue_by_signature("gfxstream::host::BackendCallbacks")
        self.assertIsNotNone(issue)
        self.assertEqual(issue["id"], "567580358")
        self.assertEqual(issue["status"], "NEW")
        self.assertIsNone(client.token)

    def test_clean_version_string(self):
        self.assertEqual(clean_version_string("37.1.3-16373346"), "37.1.3")
        self.assertEqual(clean_version_string("37.1.2-16173978"), "37.1.2")
        self.assertEqual(clean_version_string("0.0.1-dev"), "0.0.1")
        self.assertEqual(clean_version_string("37.1.4"), "37.1.4")
        self.assertEqual(clean_version_string(None), "")
        self.assertEqual(clean_version_string(""), "")

    @patch("buganizer.subprocess.run")
    @patch(
        "buganizer.shutil.which",
        side_effect=lambda name, **kwargs: (
            "/google/bin/releases/issues-cli/issues" if name == "issues" else None
        ),
    )
    def test_get_or_create_hotlist_found(self, mock_which, mock_run):
        mock_run.return_value = MagicMock(
            returncode=0,
            stdout=(
                "Hotlist ID: 778899\n"
                "Hotlist Name: qemu-next-crashes\n"
                "Hotlist Description: General tracking hotlist\n"
                "\n"
            ),
        )
        client = BuganizerClient(token=None)
        hl_id = client.get_or_create_hotlist("qemu-next-crashes")
        self.assertEqual(hl_id, 778899)
        self.assertEqual(client._hotlist_cache["qemu-next-crashes"], 778899)

    @patch("buganizer.subprocess.run")
    @patch(
        "buganizer.shutil.which",
        side_effect=lambda name, **kwargs: (
            "/google/bin/releases/issues-cli/issues" if name == "issues" else None
        ),
    )
    def test_get_or_create_hotlist_creates_when_missing(self, mock_which, mock_run):
        # First call: search returns non-zero/empty without crashing. Second call: create returns created ID
        mock_run.side_effect = [
            MagicMock(returncode=1, stdout="No hotlists found matching query.\n"),
            MagicMock(returncode=0, stdout="Created hotlist ID: 998877\n"),
        ]
        client = BuganizerClient(token=None)
        hl_id = client.get_or_create_hotlist("qemu-next-crashes-version-37.1.3")
        self.assertEqual(hl_id, 998877)
        self.assertEqual(
            client._hotlist_cache["qemu-next-crashes-version-37.1.3"], 998877
        )

    @patch("buganizer.subprocess.run")
    @patch(
        "buganizer.shutil.which",
        side_effect=lambda name, **kwargs: (
            "/google/bin/releases/issues-cli/issues" if name == "issues" else None
        ),
    )
    def test_add_issue_to_hotlist(self, mock_which, mock_run):
        mock_run.return_value = MagicMock(returncode=0)
        client = BuganizerClient(token=None)
        ok = client.add_issue_to_hotlist(123456, 778899)
        self.assertTrue(ok)
        self.assertIn("add-hotlist", mock_run.call_args[0][0])
        self.assertIn("--issue_id=123456", mock_run.call_args[0][0])
        self.assertIn("--hotlist_id=778899", mock_run.call_args[0][0])

    @patch.object(BuganizerClient, "add_issue_to_hotlist", return_value=True)
    @patch.object(BuganizerClient, "get_or_create_hotlist")
    def test_ensure_crash_hotlists(self, mock_get_or_create, mock_add):
        mock_get_or_create.side_effect = [111, 222]
        client = BuganizerClient(token=None)
        hl_ids = client.ensure_crash_hotlists(555666, version="37.1.3-16373346")
        self.assertEqual(hl_ids, [111, 222])
        self.assertEqual(mock_get_or_create.call_count, 2)
        mock_get_or_create.assert_any_call(
            GENERAL_CRASHES_HOTLIST_NAME,
            description="General tracking hotlist for all qemu-next production crashes",
        )
        mock_get_or_create.assert_any_call(
            f"{VERSION_CRASHES_HOTLIST_PREFIX}37.1.3",
            description="Tracking hotlist for qemu-next crashes observed in version 37.1.3",
        )

    def test_has_new_crash_information_identical_returns_false(self):
        existing_issue = {
            "title": "[Crash][Linux] Host: Vulkan pipeline crash",
            "description": "Crash on Linux with build 16300000. internal-msg: out of memory",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = [
            "Additional crash on Linux, build 16300000, internal-msg: out of memory"
        ]
        new_crash = {
            "os": ["Linux"],
            "build_ids": ["16300000"],
            "custom_keys": {"internal-msg": "out of memory"},
            "crash_location": "Host (vulkan.cc:10)",
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertFalse(has_new)
        self.assertEqual(reasons, [])

    def test_has_new_crash_information_new_platform_returns_true(self):
        existing_issue = {
            "title": "[Crash][Linux] Host: Vulkan pipeline crash",
            "description": "Crash on Linux with build 16300000",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Mac OS X"],
            "build_ids": ["16300000"],
            "crash_location": "Host (vulkan.cc:10)",
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)
        self.assertTrue(any("macOS" in r for r in reasons))

    def test_has_new_crash_information_word_boundary_os_detection(self):
        # Description contains 'window' and 'machine', but neither Windows nor macOS has been observed yet
        existing_issue = {
            "title": "[Crash][Linux] Host: Vulkan pipeline crash",
            "description": "Failed to create guest window inside Linux machine virtual space.",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = []

        new_crash_win = {
            "os": ["Windows 11"],
            "build_ids": ["16300000"],
            "crash_location": "Host (vulkan.cc:10)",
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash_win
        )
        self.assertTrue(has_new)
        self.assertTrue(any("Windows" in r for r in reasons))

        new_crash_mac = {
            "os": ["Mac OS X"],
            "build_ids": ["16300000"],
            "crash_location": "Host (vulkan.cc:10)",
        }
        has_new_mac, reasons_mac = has_new_crash_information(
            existing_issue, existing_comments, new_crash_mac
        )
        self.assertTrue(has_new_mac)
        self.assertTrue(any("macOS" in r for r in reasons_mac))

    def test_has_new_crash_information_new_error_message_returns_true(self):
        existing_issue = {
            "title": "[Crash][Linux] Launcher crash",
            "description": "Crash occurred on Linux.",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Linux"],
            "custom_keys": {"internal-msg": "Failed to bind port 5554: Address already in use"},
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)
        self.assertTrue(any("Failed to bind port 5554" in r for r in reasons))

    def test_has_new_crash_information_regression_on_closed_returns_true(self):
        existing_issue = {
            "title": "[Crash][Linux] Fixed crash",
            "description": "Fixed in CL 12345.",
            "issueState": {"status": "FIXED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Linux"],
            "build_ids": ["16400000"],
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)
        self.assertTrue(any("regression" in r.lower() for r in reasons))

    def test_format_new_crash_info_comment(self):
        reasons = [
            "New host platform: First occurrence observed on macOS (Mac OS X)",
            "New internal error message: 'Failed to bind port'",
        ]
        new_crash = {
            "os": ["Mac OS X"],
            "versions": ["37.1.3-16373346"],
            "build_ids": ["16373346"],
            "crash_location": "setup_ports (launcher.cc:93)",
        }
        md = format_new_crash_info_comment("190d58d170400000", reasons, new_crash)
        self.assertIn("http://go/crash/190d58d170400000", md)
        self.assertIn("First occurrence observed on macOS", md)
        self.assertIn("setup_ports (launcher.cc:93)", md)

    def test_has_new_crash_information_none_fields_handled_safely(self):
        existing_issue = {
            "title": None,
            "description": None,
            "issueState": None,
        }
        existing_comments = [None, "Some comment"]
        new_crash = {
            "os": ["Linux"],
            "build_ids": ["16300000"],
            "crash_location": "Host (vulkan.cc:10)",
        }
        # Verify no TypeError is raised
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)

    def test_has_new_crash_information_regression_empty_build_id(self):
        existing_issue = {
            "title": "[Crash][Linux] Fixed crash",
            "description": "Fixed in CL 12345.",
            "issueState": {"status": "FIXED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Linux"],
            "build_ids": [""],
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)
        self.assertFalse(any("on Build ID  " in r or r.endswith("on Build ID ") for r in reasons))

    def test_format_new_crash_info_comment_scalar_strings(self):
        reasons = ["New platform observed"]
        new_crash = {
            "os": "Linux",
            "versions": "37.1.3",
            "build_ids": "16300000",
            "crash_location": "setup_ports",
        }
        md = format_new_crash_info_comment("190d58d170400000", reasons, new_crash)
        self.assertIn("- **Host OS:** `Linux`", md)
        self.assertNotIn("L, i, n, u, x", md)
        self.assertIn("- **Build IDs / Version:** `16300000` (`37.1.3`)", md)

    def test_parse_hotlists_cli_output_consecutive_records(self):
        raw = (
            "Hotlist ID: 111\n"
            "Hotlist Name: qemu-next-crashes\n"
            "Hotlist Description: General\n"
            "Hotlist ID: 222\n"
            "Hotlist Name: qemu-next-crashes-version-37.1.3\n"
            "Hotlist Description: Milestone\n"
        )
        parsed = BuganizerClient._parse_hotlists_cli_output(raw)
        self.assertEqual(len(parsed), 2)
        self.assertEqual(parsed[0]["id"], 111)
        self.assertEqual(parsed[0]["name"], "qemu-next-crashes")
        self.assertEqual(parsed[1]["id"], 222)
        self.assertEqual(parsed[1]["name"], "qemu-next-crashes-version-37.1.3")

    def test_has_new_crash_information_truncated_snippet_containment(self):
        long_msg = "A" * 250
        truncated_snippet = long_msg[:200]
        existing_issue = {
            "title": "[Crash][Linux] Test",
            "description": "Some description",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = [f"New internal error message: '{truncated_snippet}'"]
        new_crash = {
            "os": ["Linux"],
            "custom_keys": {"internal-msg": long_msg},
        }
        # Because we compare snippet[:200] against all_text, it should find it and return False
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertFalse(has_new)
        self.assertEqual(reasons, [])

    def test_has_new_crash_information_strips_guest_host_prefix(self):
        existing_issue = {
            "title": "[Crash][Linux] Vulkan error",
            "description": "Vulkan error in vulkan_pipeline.cc:123",
            "issueState": {"status": "ASSIGNED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Linux"],
            "crash_location": "Guest (vulkan_pipeline.cc:123)",
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertFalse(has_new)
        # Verify "Guest" was not extracted as a function name
        self.assertFalse(any("Different faulting function: Guest" in r for r in reasons))

        # Verify divergent location after "Host: " or "Host (" is detected
        divergent_crash = {
            "os": ["Linux"],
            "crash_location": "Host: decode_frame (decoder.cc:42)",
        }
        has_new_div, reasons_div = has_new_crash_information(
            existing_issue, existing_comments, divergent_crash
        )
        self.assertTrue(has_new_div)
        self.assertTrue(
            any("Different faulting function: decode_frame" in r for r in reasons_div)
        )

        # Verify raw hexadecimal memory addresses (unsymbolicated frames) are ignored
        hex_crash = {
            "os": ["Linux"],
            "crash_location": "Host: 0x7f9a12345678",
        }
        has_new_hex, reasons_hex = has_new_crash_information(
            existing_issue, existing_comments, hex_crash
        )
        self.assertFalse(has_new_hex)
        self.assertEqual(reasons_hex, [])

    @patch("buganizer.subprocess.run")
    @patch(
        "buganizer.shutil.which",
        side_effect=lambda name, **kwargs: (
            "/usr/bin/base_cli" if name == "base_cli" else None
        ),
    )
    def test_get_or_create_hotlist_creates_with_base_cli(self, mock_which, mock_run):
        mock_run.side_effect = [
            # 1. hotlist search returns empty []
            MagicMock(returncode=0, stdout="[]"),
            # 2. hotlist create returns new hotlist dict
            MagicMock(returncode=0, stdout='{"id": 887766, "name": "qemu-next-crashes"}'),
        ]
        client = BuganizerClient(token=None)
        self.assertEqual(client.cli_binary, "/usr/bin/base_cli")
        hl_id = client.get_or_create_hotlist("qemu-next-crashes")
        self.assertEqual(hl_id, 887766)
        self.assertEqual(client._hotlist_cache["qemu-next-crashes"], 887766)

    def test_has_new_crash_information_regression_scalar_build_id_and_dedup(self):
        existing_issue = {
            "title": "[Crash][Linux] Fixed crash",
            "description": "Fixed in CL 12345.",
            "issueState": {"status": "FIXED"},
        }
        existing_comments = []
        new_crash = {
            "os": ["Linux"],
            "build_ids": "16400000",
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, existing_comments, new_crash
        )
        self.assertTrue(has_new)
        self.assertTrue(any("on Build ID 16400000" in r for r in reasons))

        # Subsequent run where the regression comment is already in existing_comments
        existing_comments_with_reg = [reasons[0]]
        has_new_dup, reasons_dup = has_new_crash_information(
            existing_issue, existing_comments_with_reg, new_crash
        )
        self.assertFalse(has_new_dup)
        self.assertEqual(reasons_dup, [])

    def test_parse_created_hotlist_id_ignores_unanchored_numbers(self):
        self.assertEqual(
            BuganizerClient._parse_created_hotlist_id("Created hotlist ID: 998877"),
            998877,
        )
        self.assertEqual(
            BuganizerClient._parse_created_hotlist_id("20261005 19:35:00 Error 50000"),
            0,
        )

    def test_clean_version_string_handles_non_string_scalar(self):
        self.assertEqual(clean_version_string(16373346), "16373346")
        self.assertEqual(clean_version_string("37.1.3-16373346"), "37.1.3")
        self.assertEqual(clean_version_string(None), "")

    def test_has_new_crash_information_mac_address_and_quick_win_do_not_suppress_os(self):
        existing_issue = {
            "title": "[Crash][Linux] Network interface error",
            "description": "Failed to configure MAC address on Linux. A quick win is to retry.",
            "issueState": {"status": "ASSIGNED"},
        }
        new_crash_mac = {
            "os": ["Mac OS X"],
        }
        has_new, reasons = has_new_crash_information(existing_issue, [], new_crash_mac)
        self.assertTrue(has_new)
        self.assertTrue(any("macOS" in r for r in reasons))

        new_crash_win = {
            "os": ["Windows 11"],
        }
        has_new_w, reasons_w = has_new_crash_information(existing_issue, [], new_crash_win)
        self.assertTrue(has_new_w)
        self.assertTrue(any("Windows" in r for r in reasons_w))

    def test_has_new_crash_information_multiline_msg_and_non_string_scalars(self):
        existing_issue = {
            "title": "[Crash][Linux] Crash",
            "description": "Crash on Linux",
            "issueState": {"status": "ASSIGNED"},
        }
        new_crash = {
            "os": 123,
            "versions": 37,
            "build_ids": 16400000,
            "custom_keys": {"internal-msg": "line one\nline two\n   line three"},
        }
        has_new, reasons = has_new_crash_information(existing_issue, [], new_crash)
        self.assertTrue(has_new)
        self.assertIn("New internal error message: 'line one line two line three'", reasons)
        md = format_new_crash_info_comment("1234", reasons, new_crash)
        self.assertIn("- **Build IDs / Version:** `16400000` (`37`)", md)

    def test_parse_hotlists_cli_output_supports_hotlist_title(self):
        raw = (
            "Hotlist ID: 333\n"
            "Hotlist Title: qemu-next-crashes\n"
            "Hotlist Description: General\n"
        )
        parsed = BuganizerClient._parse_hotlists_cli_output(raw)
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0]["id"], 333)
        self.assertEqual(parsed[0]["name"], "qemu-next-crashes")

    @patch("buganizer.subprocess.run")
    @patch(
        "buganizer.shutil.which",
        side_effect=lambda name, **kwargs: (
            "/usr/bin/base_cli" if name == "base_cli" else None
        ),
    )
    def test_hotlist_cli_dict_unwrapping_and_qa_mode(self, mock_which, mock_run):
        mock_run.side_effect = [
            MagicMock(
                returncode=0,
                stdout='{"hotlists": [{"id": 998811, "name": "qemu-next-crashes"}]}',
            ),
            MagicMock(returncode=0, stdout="{}"),
        ]
        client = BuganizerClient(token=None, qa_mode=True)
        hl_id = client.get_or_create_hotlist("qemu-next-crashes")
        self.assertEqual(hl_id, 998811)
        search_cmd = mock_run.call_args_list[0][0][0]
        self.assertIn("--api=blade:corp-issuetracker-test-api", search_cmd)

        ok = client.add_issue_to_hotlist(123456, 998811)
        self.assertTrue(ok)
        add_cmd = mock_run.call_args_list[1][0][0]
        self.assertIn("--api=blade:corp-issuetracker-test-api", add_cmd)

    def test_has_new_crash_information_none_status(self):
        existing_issue = {
            "title": "[Crash][Linux] Vulkan error",
            "description": "Vulkan error on Linux",
            "status": None,
            "issueState": {"status": None},
        }
        has_new, reasons = has_new_crash_information(
            existing_issue, [], {"os": ["Linux"]}
        )
        self.assertFalse(has_new)
        self.assertEqual(reasons, [])


if __name__ == "__main__":
    unittest.main()
