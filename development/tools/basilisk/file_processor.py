import re
from pathlib import Path
from typing import Optional

from development.tools.basilisk.config import Config


class FileProcessor:
    """Processes individual files to determine refactoring actions."""

    def __init__(self, config: Config):
        """Initializes the FileProcessor.

        Args:
            config: The configuration object.
        """
        self.config = config

    def to_snake_case(self, name: str) -> str:
        """Converts a string from PascalCase or camelCase to snake_case.

        Args:
            name: The string to convert.

        Returns:
            The converted string in snake_case.
        """
        s1 = re.sub("(.)([A-Z][a-z]+)", r"\1_\2", name)
        s2 = re.sub("([a-z0-9])([A-Z])", r"\1_\2", s1).lower()
        return re.sub(r"_+", "_", s2)

    def find_module_root(self, file_path: Path) -> Optional[Path]:
        """Finds the Bazel module root for a given file.

        The module root is the directory containing the closest BUILD or BUILD.bazel file.

        Args:
            file_path: The path to the file.

        Returns:
            The path to the module root, or None if not found.
        """
        current = file_path.parent
        while current != self.config.REPO_ROOT:
            if (current / "BUILD").exists() or (current / "BUILD.bazel").exists():
                return current
            if current == current.parent:
                break
            current = current.parent
        return None
