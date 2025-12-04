import re
from typing import Optional


class NamespaceParser:
    """A parser for extracting namespace information from C++ header files."""

    _SANITIZE_PATTERN = re.compile(
        r'("(?:\\.|[^"\\])*")|'
        r"('(?:\\.|[^'\\])*')|"
        r"(/\*[\s\S]*?\*/)|"
        r"(//.*?$)",
        re.MULTILINE | re.DOTALL,
    )
    _NAMESPACE_DEF_PATTERN = re.compile(r"\bnamespace\s+([a-zA-Z0-9_:]+)\s*\{")
    _NAMESPACE_KEYWORD_PATTERN = re.compile(r"\bnamespace\b")

    @classmethod
    def _sanitize(cls, content: str) -> str:
        """Removes comments and strings from C++ code content.

        Args:
            content: The C++ code content.

        Returns:
            The sanitized code content with comments and strings replaced by whitespace.
        """

        def mask_match(match):
            return " " * len(match.group(0))

        return cls._SANITIZE_PATTERN.sub(mask_match, content)

    @classmethod
    def contains_namespace(cls, content: str) -> bool:
        """Checks if the given C++ code content contains a namespace definition.

        Args:
            content: The C++ code content.

        Returns:
            True if a namespace is found, False otherwise.
        """
        clean_content = cls._sanitize(content)
        return bool(cls._NAMESPACE_KEYWORD_PATTERN.search(clean_content))

    @classmethod
    def extract_namespace(cls, content: str) -> Optional[str]:
        """Extracts the first namespace from the given C++ code content.

        This method is designed to find the primary namespace of a header file,
        ignoring namespaces inside #if blocks.

        Args:
            content: The C++ code content.

        Returns:
            The namespace as a path-like string (e.g., "my_namespace/nested"),
            or None if no namespace is found.
        """
        clean_content = cls._sanitize(content)
        lines = clean_content.splitlines()
        filtered_lines = []
        depth = 0
        header_guard_seen = False

        for line in lines:
            stripped = line.strip()
            if stripped.startswith("#"):
                if stripped.startswith("#if"):
                    if stripped.startswith("#ifndef") and not header_guard_seen:
                        header_guard_seen = True
                    else:
                        depth += 1
                elif stripped.startswith("#endif"):
                    if depth > 0:
                        depth -= 1
                continue

            if depth == 0:
                filtered_lines.append(line)
            else:
                filtered_lines.append("")

        final_content = "".join(filtered_lines)
        matches = cls._NAMESPACE_DEF_PATTERN.findall(final_content)
        if not matches:
            return None

        flat_chain = []
        for match in matches:
            parts = match.split("::")
            flat_chain.extend(parts)
        return "/".join(flat_chain) if flat_chain else None
