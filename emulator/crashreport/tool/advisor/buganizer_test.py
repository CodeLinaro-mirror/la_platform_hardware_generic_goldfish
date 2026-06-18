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


if __name__ == "__main__":
    unittest.main()
