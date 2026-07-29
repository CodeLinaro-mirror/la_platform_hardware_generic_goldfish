# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""End-to-end live verification tests for BuganizerClient on macOS/GWindows.

Designed to run locally against production ComponentId 73415 (TestBugDumpingGround)
using local developer OAuth2 tokens and Cloud Project quota headers.
"""

import os
import sys
import unittest

from buganizer import BuganizerClient, BuganizerError


class TestBuganizerLiveE2E(unittest.TestCase):
    """Live E2E test suite targeting production Playground component (73415)."""

    def setUp(self) -> None:
        self.token = os.environ.get("BUGANIZER_TOKEN")
        self.api_key = os.environ.get("BUGANIZER_API_KEY") or os.environ.get(
            "X_GOOG_API_KEY"
        )
        self.user_project = os.environ.get("BUGANIZER_USER_PROJECT") or os.environ.get(
            "X_GOOG_USER_PROJECT"
        )

        if not self.token:
            self.skipTest(
                "Skipping live E2E test: BUGANIZER_TOKEN not set in environment."
            )
        self.stable_sig = "E2ETest::LiveVerification-9999"

    def test_live_production_playground_flow(self) -> None:
        """Test interaction with production Miscellaneous > TestBugDumpingGround (ComponentId 73415)."""
        client = BuganizerClient(token=self.token, component_id=73415, verbose=True)
        try:
            # 1. Search for existing issue in 73415
            issue = client.search_issue_by_signature(self.stable_sig)
            if not issue:
                # 2. Create new issue in 73415
                issue_id = client.create_issue(
                    self.stable_sig,
                    "Live E2E Verification Test in TestBugDumpingGround (73415).",
                )
                self.assertGreater(issue_id, 0)
            else:
                issue_id = int(issue.get("id", 0))
                # 3. Update existing issue comment
                client.update_issue_comment(
                    issue_id, "Live E2E comment update verification in 73415."
                )
        except BuganizerError as e:
            self.fail(f"Live production playground E2E flow failed: {e}")


if __name__ == "__main__":
    unittest.main()
