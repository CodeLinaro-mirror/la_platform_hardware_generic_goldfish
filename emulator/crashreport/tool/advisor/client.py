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

"""Encapsulates execution and verification of the gosso authentication binary."""

import logging
import shutil
import subprocess
from pathlib import Path
from typing import Optional


class GossoClient:
    """Encapsulates execution and verification of the gosso authentication binary."""

    def __init__(self, gosso_path: Optional[str] = None) -> None:
        self.gosso_path = gosso_path or shutil.which("gosso")
        if not self.gosso_path:
            raise RuntimeError(
                "The 'gosso' binary was not found in PATH.\n"
                "gosso is required for non-interactive UberProxy authentication to fetch minidumps.\n"
                "Please install gosso (see http://go/gosso-doc) or manually download the minidump."
            )

    def fetch_url_to_file(self, url: str, output_path: Path) -> None:
        """Fetch an UberProxy protected URL directly to a file descriptor."""
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
        except subprocess.CalledProcessError as e:
            logging.error("Gosso execution failed: %s", e.stderr.decode().strip())
            if output_path.exists():
                output_path.unlink()
            raise
        except Exception as e:
            logging.error("Unexpected error during download: %s", e)
            if output_path.exists():
                output_path.unlink()
            raise
