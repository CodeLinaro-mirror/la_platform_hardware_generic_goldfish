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
    PROD_REST_ENDPOINT,
    QA_REST_ENDPOINT,
    TAG_ANALYZED,
    TAG_DISPATCHED,
    BuganizerAuthError,
    BuganizerClient,
    BuganizerError,
    derive_crash_title,
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


if __name__ == "__main__":
    unittest.main()
