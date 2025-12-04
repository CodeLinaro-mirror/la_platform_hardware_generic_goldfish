#!/usr/bin/env python3
"""
Bazel C++ Repository Standardization Tool
"""

import argparse
import logging
from pathlib import Path

from development.tools.basilisk.config import Config
from development.tools.basilisk.file_processor import FileProcessor
from development.tools.basilisk.repository import Repository
from development.tools.basilisk.refactor_manager import RefactorManager


def main():
    """The main entry point for the Basilisk refactoring tool."""
    parser = argparse.ArgumentParser(description="Refactor Bazel C++ Repo.")
    parser.add_argument(
        "--repo_root",
        type=Path,
        required=True,
        help="The root directory of the repository to refactor.",
    )
    parser.add_argument("--execute", action="store_true", help="Apply changes")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging."
    )
    args = parser.parse_args()

    # Configure logging
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format="%(message)s")

    DRY_RUN = not args.execute
    logging.info(f"Initializing Basilisk... (Dry Run: {DRY_RUN})")

    config = Config(args.repo_root.resolve())
    file_processor = FileProcessor(config)
    repository = Repository(config, file_processor)
    refactor_manager = RefactorManager(config, repository, dry_run=DRY_RUN)

    repository.scan_and_plan()
    refactor_manager.execute_moves()
    refactor_manager.update_references()
    refactor_manager.cleanup_empty_dirs()

    logging.info("Done.")


if __name__ == "__main__":
    main()
