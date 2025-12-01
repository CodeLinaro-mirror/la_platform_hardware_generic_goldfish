import logging
import os
from pathlib import Path
from collections import defaultdict
from typing import Dict, Set

from development.tools.basilisk.config import Config
from development.tools.basilisk.file_processor import FileProcessor
from development.tools.basilisk.namespace_parser import NamespaceParser


class Repository:
    """
    Represents the state of the repository and manages the refactoring plan.

    This class is responsible for scanning the file system, planning the moves
    (calculating the new path for each file), and resolving any path collisions.
    """

    def __init__(self, config: Config, file_processor: FileProcessor):
        """Initializes the Repository.

        Args:
            config: The configuration object.
            file_processor: The file processor object.
        """
        self.config = config
        self.file_processor = file_processor
        self.abs_rename_map: Dict[Path, Path] = {}
        self.claimed_paths: Set[Path] = set()
        self.file_index = defaultdict(list)

    def is_read_only(self, path: Path) -> bool:
        """Checks if a path is considered read-only.

        A path is considered read-only if it is within a directory listed in
        the READ_ONLY_DIRS set in the configuration.

        Args:
            path: The path to check.

        Returns:
            True if the path is read-only, False otherwise.
        """
        try:
            rel = path.relative_to(self.config.REPO_ROOT)
            if rel.parts[0] in self.config.READ_ONLY_DIRS:
                logging.debug(f"Path {path} is read-only.")
                return True
            if any(p in self.config.READ_ONLY_DIRS for p in rel.parts):
                logging.debug(f"Path {path} is read-only.")
                return True
        except ValueError:
            pass
        return False

    def _is_test_file(self, file_path: Path) -> bool:
        """Checks if a file is a test file."""
        return (
            "test" in file_path.parts
            or file_path.name.endswith("_test.cc")
            or file_path.name.endswith("_test.cpp")
            or file_path.name.endswith("_unittest.cc")
            or file_path.name.endswith("_unittest.cpp")
        )

    def scan_and_plan(self):
        """Scans the repository and creates a refactoring plan.

        This method walks through the repository, identifies files to be refactored,
        determines their new paths based on the rules in FileProcessor, and
        populates the abs_rename_map with the planned moves. It also resolves
        any collisions that may occur.
        """
        logging.info(f"--- 1. Scanning Repository ({self.config.REPO_ROOT}) ---")
        pending_moves = defaultdict(list)

        for root, dirs, files in os.walk(self.config.REPO_ROOT):
            dirs[:] = [d for d in dirs if d not in self.config.IGNORE_DIRS]
            root_path = Path(root)

            for file in files:
                file_path = root_path / file
                logging.debug(f"Scanning file: {file_path}")
                ext = file_path.suffix
                if ext not in [".cpp", ".cc", ".c", ".h", ".hpp"]:
                    continue

                self.file_index[file].append(file_path)

                if self.is_read_only(file_path):
                    logging.debug(f"Skipping read-only file: {file_path}")
                    continue

                # CHECK MANUAL OVERRIDES
                try:
                    rel_path_str = file_path.relative_to(
                        self.config.REPO_ROOT
                    ).as_posix()
                    if rel_path_str in self.config.MANUAL_OVERRIDES:
                        target_str = self.config.MANUAL_OVERRIDES[rel_path_str]
                        intended_path = self.config.REPO_ROOT / target_str
                        pending_moves[intended_path].append(file_path)
                        logging.debug(
                            f"Manual override: {file_path} -> {intended_path}"
                        )
                        continue
                except ValueError:
                    pass

                # C vs C++ Header Check
                is_c_header = False
                if ext in [".h", ".hpp"]:
                    try:
                        with open(
                            file_path, "r", encoding="utf-8", errors="ignore"
                        ) as f:
                            content = f.read()
                            if not NamespaceParser.contains_namespace(content):
                                is_c_header = True
                    except Exception:
                        continue

                module_root = self.file_processor.find_module_root(file_path)
                if not module_root:
                    logging.debug(f"No module root found for {file_path}, skipping.")
                    continue

                try:
                    rel_to_module = file_path.relative_to(module_root)
                    rel_parts = rel_to_module.parts
                except ValueError:
                    continue

                is_in_src_dir = "src" in rel_parts
                is_in_include_dir = "include" in rel_parts
                is_test = self._is_test_file(file_path)

                is_public_header = is_in_include_dir and ext in [".h", ".hpp"]
                is_private_header = is_in_src_dir and ext in [".h", ".hpp"]
                is_cpp_source = ext in [".cpp", ".cc"]
                is_c_source = ext == ".c"

                new_stem = self.file_processor.to_snake_case(file_path.stem)
                target_base_dir = None
                new_ext = ext

                # --- LOGIC BRANCHING ---

                if is_c_header:
                    # C Headers: Rename (snake_case) only. DO NOT MOVE.
                    target_base_dir = file_path.parent
                    new_ext = ".h"

                elif is_c_source:
                    # C Files: Flatten to src/, but keep .c extension
                    target_base_dir = module_root / "src"
                    new_ext = ".c"

                elif is_cpp_source or is_private_header:
                    # C++ Source & Private Headers: Flatten to src/
                    # UNLESS it's a test file.
                    if not is_test:
                        target_base_dir = module_root / "src"
                    else:
                        # Keep tests in their original directory structure
                        target_base_dir = file_path.parent

                    if ext == ".cpp":
                        new_ext = ".cc"
                    if is_private_header:
                        new_ext = ".h"

                elif is_public_header:
                    # C++ Public Header: Nest by Namespace
                    new_ext = ".h"
                    base_include = module_root / "include"
                    try:
                        with open(
                            file_path, "r", encoding="utf-8", errors="ignore"
                        ) as f:
                            ns_path_str = NamespaceParser.extract_namespace(f.read())
                    except Exception:
                        ns_path_str = None

                    target_base_dir = (
                        base_include / ns_path_str if ns_path_str else base_include
                    )

                if target_base_dir:
                    intended_path = target_base_dir / (new_stem + new_ext)
                    logging.debug(f"Plan: {file_path} -> {intended_path}")
                    pending_moves[intended_path].append(file_path)

        logging.info("--- 2. Resolving Collisions ---")
        for target, originals in pending_moves.items():
            if len(originals) > 1:
                logging.debug(
                    f"Collision detected for target {target}. Originals: {originals}"
                )
            for orig in originals:
                final_path = self._resolve_path(orig, target)
                self.abs_rename_map[orig] = final_path
                self.claimed_paths.add(final_path)
                logging.debug(f"Resolved mapping: {orig} -> {final_path}")

    def _resolve_path(self, original_path: Path, intended_target: Path) -> Path:
        """Resolves path collisions by prepending parent directory names.

        If a file's intended new path is already taken, this method will
        attempt to create a new, unique path by prepending the names of the
        original file's parent directories until a free path is found.

        Args:
            original_path: The original path of the file.
            intended_target: The desired new path for the file.

        Returns:
            A unique, resolved path for the file.
        """
        candidate = intended_target
        if not candidate.exists() and candidate not in self.claimed_paths:
            return candidate

        target_dir = intended_target.parent
        base_name = intended_target.name
        current_parent_idx = 0
        parents = list(original_path.parents)

        while True:
            is_taken_on_disk = candidate.exists()
            is_claimed_in_run = candidate in self.claimed_paths

            if (
                is_taken_on_disk
                and candidate == original_path
                and not is_claimed_in_run
            ):
                return candidate
            if not is_taken_on_disk and not is_claimed_in_run:
                return candidate

            if current_parent_idx >= len(parents):
                logging.error(f"Could not resolve collision for {original_path}")
                return intended_target

            parent_name = parents[current_parent_idx].name.lower()
            if parent_name in ["src", "include"]:
                current_parent_idx += 1
                continue

            new_name = f"{parent_name}_{base_name}"
            candidate = target_dir / new_name
            current_parent_idx += 1
            base_name = new_name
