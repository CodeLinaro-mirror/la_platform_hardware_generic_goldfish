import logging
import os
import re
import subprocess
from pathlib import Path
from typing import Optional

from development.tools.basilisk.config import Config
from development.tools.basilisk.repository import Repository


class RefactorManager:
    """
    Executes the refactoring plan.

    This class is responsible for all I/O operations, including moving files,
    updating file content (includes and BUILD files), and cleaning up empty
    directories.
    """

    def __init__(self, config: Config, repository: Repository, dry_run: bool = True):
        """Initializes the RefactorManager.

        Args:
            config: The configuration object.
            repository: The repository object containing the refactoring plan.
            dry_run: If True, no changes will be written to the file system.
        """
        self.config = config
        self.repository = repository
        self.dry_run = dry_run

    def execute_moves(self):
        """Executes the file move operations based on the plan."""
        logging.info("--- 3. Executing Moves ---")
        count = 0
        for old, new in self.repository.abs_rename_map.items():
            if old == new:
                continue

            rel_old = old.relative_to(self.config.REPO_ROOT)
            rel_new = new.relative_to(self.config.REPO_ROOT)
            logging.info(f"MV: {rel_old} -> {rel_new}")

            if not self.dry_run:
                if not new.parent.exists():
                    logging.debug(f"Creating directory {new.parent}")
                    new.parent.mkdir(parents=True, exist_ok=True)
                try:
                    subprocess.check_call(
                        ["git", "mv", str(old), str(new)],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                except (subprocess.CalledProcessError, FileNotFoundError):
                    logging.debug(
                        f"git mv failed for {old}, falling back to os.rename."
                    )
                    os.rename(old, new)
            count += 1
        logging.info(f"Moved {count} files.")

    def resolve_include_path(
        self, current_file: Path, include_str: str
    ) -> Optional[Path]:
        """
        Resolves an #include string to its new path after refactoring.

        It uses several strategies to find the included file:
        1.  Physical relative path from the including file.
        2.  Absolute path from the repository root.
        3.  Fuzzy suffix match against all files in the repository.

        Args:
            current_file: The file containing the #include directive.
            include_str: The path string from the #include directive.

        Returns:
            The resolved absolute path of the included file, or None if not found.
        """
        logging.debug(f"Resolving include '{include_str}' from file '{current_file}'")
        if include_str in self.config.STD_LIB_HEADERS:
            logging.debug(f"'{include_str}' is a standard library header, skipping.")
            return None

        # 1. Physical Relative
        candidate = (current_file.parent / include_str).resolve()
        if candidate in self.repository.abs_rename_map:
            logging.debug(
                f"Resolved '{include_str}' via physical relative path to '{self.repository.abs_rename_map[candidate]}'"
            )
            return self.repository.abs_rename_map[candidate]
        elif candidate.exists():
            logging.debug(
                f"Resolved '{include_str}' via physical relative path to '{candidate}'"
            )
            return candidate

        # 2. Repo Absolute
        candidate = (self.config.REPO_ROOT / include_str).resolve()
        if candidate in self.repository.abs_rename_map:
            logging.debug(
                f"Resolved '{include_str}' via repo absolute path to '{self.repository.abs_rename_map[candidate]}'"
            )
            return self.repository.abs_rename_map[candidate]
        elif candidate.exists():
            logging.debug(
                f"Resolved '{include_str}' via repo absolute path to '{candidate}'"
            )
            return candidate

        # 3. Suffix / Fuzzy Match
        filename = Path(include_str).name
        if filename in self.repository.file_index:
            candidates = self.repository.file_index[filename]
            for c in candidates:
                c_str = c.as_posix()
                inc_str = include_str.replace("\\\\", "/")
                if c_str.endswith(inc_str):
                    if c in self.repository.abs_rename_map:
                        logging.debug(
                            f"Resolved '{include_str}' via fuzzy match to '{self.repository.abs_rename_map[c]}'"
                        )
                        return self.repository.abs_rename_map[c]
                    logging.debug(f"Resolved '{include_str}' via fuzzy match to '{c}'")
                    return c

        logging.debug(f"Could not resolve include path for '{include_str}'")
        return None

    def update_references(self):
        """
        Updates all file references (in #includes and BUILD files) across the repo.

        This method scans all relevant source and build files and replaces any
        references to old file paths with their new, refactored paths.
        """
        logging.info("--- 4. Updating Content (Global Scan) ---")

        files_to_scan = set()
        for root, dirs, files in os.walk(self.config.REPO_ROOT):
            dirs[:] = [d for d in dirs if d not in self.config.IGNORE_DIRS]
            for f in files:
                fpath = Path(root) / f
                if f in ["BUILD", "BUILD.bazel"]:
                    files_to_scan.add(fpath)
                elif fpath.suffix in [".cc", ".cpp", ".c", ".h", ".hpp"]:
                    files_to_scan.add(fpath)

        for new_path in self.repository.abs_rename_map.values():
            files_to_scan.add(new_path)

        for fpath in files_to_scan:
            logging.debug(f"Scanning for references in {fpath}")
            if not fpath.exists():
                continue
            if self.repository.is_read_only(fpath):
                continue

            original_path_context = fpath
            for old, new in self.repository.abs_rename_map.items():
                if new == fpath:
                    original_path_context = old
                    break

            try:
                with open(fpath, "r", encoding="utf-8") as f:
                    content = f.read()
            except UnicodeDecodeError:
                continue

            new_content = content
            is_build_file = fpath.name in ["BUILD", "BUILD.bazel"]

            # --- A. Update BUILD Files ---
            if is_build_file:
                build_dir = fpath.parent
                for old_p, new_p in self.repository.abs_rename_map.items():
                    if old_p == new_p:
                        continue
                    try:
                        old_rel = old_p.relative_to(build_dir).as_posix()
                        new_rel = new_p.relative_to(build_dir).as_posix()
                    except ValueError:
                        continue

                    safe_old = re.escape(old_rel)
                    pattern = f"([\"']){safe_old}([\"'])"
                    new_content = re.sub(pattern, f"\\1{new_rel}\\2", new_content)

            # --- B. Update Includes ---
            else:

                def replace_include(match):
                    original_include = match.group(0)
                    quote_start = match.group(1)
                    inc_path = match.group(2)
                    quote_end = match.group(3)

                    logging.debug(f"Found include: {original_include}")
                    logging.debug(
                        f"  - Quote start: '{quote_start}', path: '{inc_path}', quote end: '{quote_end}'"
                    )

                    dest_path = self.resolve_include_path(
                        original_path_context, inc_path
                    )

                    if dest_path:
                        dest_module = self.repository.file_processor.find_module_root(
                            dest_path
                        )
                        if dest_module:
                            try:
                                rel_parts = dest_path.relative_to(dest_module).parts
                                is_src = "src" in rel_parts
                                is_test = "test" in rel_parts
                                is_include = "include" in rel_parts

                                if is_src or is_test:
                                    # For C files in src (nested) OR Flat C++ files
                                    # Calculate relative to src/ root if possible
                                    try:
                                        if is_src:
                                            src_root = dest_module / "src"
                                            rel_path = dest_path.relative_to(
                                                src_root
                                            ).as_posix()
                                        else:
                                            src_root = dest_module / "src"
                                            rel_path = dest_path.relative_to(
                                                src_root
                                            ).as_posix()
                                        new_include = f"#include {quote_start}{rel_path}{quote_end}"
                                        logging.debug(
                                            f"  -> Replacing with (src relative): {new_include}"
                                        )
                                        return new_include
                                    except ValueError:
                                        pass

                                elif is_include:
                                    # For C files in include (nested) OR Nested C++ files
                                    try:
                                        idx = rel_parts.index("include")
                                        clean_path = "/".join(rel_parts[idx + 1 :])
                                        new_include = f"#include {quote_start}{clean_path}{quote_end}"
                                        logging.debug(
                                            f"  -> Replacing with (include relative): {new_include}"
                                        )
                                        return new_include
                                    except ValueError:
                                        pass
                            except ValueError:
                                pass

                        try:
                            new_inc_str = dest_path.relative_to(
                                self.config.REPO_ROOT
                            ).as_posix()
                            new_include = (
                                f"#include {quote_start}{new_inc_str}{quote_end}"
                            )
                            logging.debug(
                                f"  -> Replacing with (repo absolute): {new_include}"
                            )
                            return new_include
                        except ValueError:
                            pass

                    logging.debug(f"  -> No change for include: {original_include}")
                    return original_include

                new_content = re.sub(
                    r"#include\s+([\"<])(.*)([\">])", replace_include, new_content
                )

            if new_content != content:
                logging.info(f"UPD: {fpath.relative_to(self.config.REPO_ROOT)}")
                if not self.dry_run:
                    logging.debug(f"Writing changes to {fpath}")
                    with open(fpath, "w", encoding="utf-8") as f:
                        f.write(new_content)

    def cleanup_empty_dirs(self):
        """Removes any empty directories left over after the refactoring."""
        logging.info("--- 5. Cleanup ---")
        if self.dry_run:
            logging.info("Skipping cleanup in dry-run mode.")
            return
        for root, dirs, files in os.walk(self.config.REPO_ROOT, topdown=False):
            if Path(root) == self.config.REPO_ROOT:
                continue
            if self.repository.is_read_only(Path(root)):
                continue
            if not os.listdir(root):
                try:
                    logging.debug(f"Removing empty directory: {root}")
                    os.rmdir(root)
                except OSError as e:
                    logging.warning(f"Could not remove directory {root}: {e}")
