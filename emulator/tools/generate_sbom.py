#!/usr/bin/env python3
# Copyright 2026 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generates an SPDX 2.3 JSON Software Bill of Materials (SBOM) for the Android Emulator.

Complies with Google's SPDX 2.3 style guide (go/spdx-style-guide) and
Android SBOM specifications (go/android-sbom).

Every license() target reached from the release binaries becomes one SPDX
package. Versions, download locations, PURLs, and suppliers are discovered at
build time from machine-readable sources only, in priority order:
  1. Explicit Bazel `license()` and `package_info()` attributes.
  2. Structured metadata files inside the Bazel package that declares the
     license() target: `METADATA`, the `Version:`/`URL:` fields of
     `README.{chromium,crashpad,breakpad,android,version}`, `Cargo.toml`,
     `meson.build`, `VERSION`/`_VERSION`, `curlver.h`, `MODULE.bazel`, and
     `CMakeLists.txt`. The search never leaves that package, so vendored
     sub-packages do not inherit the metadata of the repository containing them.
  3. For the root package of a Bzlmod module, the workspace Bzlmod metadata:
     `MODULE.bazel.lock`, `build/bazel/toplevel.MODULE.bazel` (with the files it
     includes), and `build/bazel/registry/modules/*/*/source.json`.
Free-form README prose is never parsed. Values that cannot be discovered are
reported as "unspecified" (versions) or NOASSERTION (download locations and
suppliers) instead of being guessed.

These metadata files cannot all be declared as Bazel action inputs, so the
generate_sbom_file() rule in sbom.bzl runs this tool unsandboxed and keeps its
result out of the disk and remote caches. Problems that would silently degrade
the SBOM, such as unreadable or malformed inputs or a missing workspace root,
are logged as warnings on stderr.
"""

import argparse
from dataclasses import dataclass, field
import glob
import hashlib
import json
import logging
import os
import re
import sys
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple
from urllib.parse import quote, urlsplit
import uuid

_LOG = logging.getLogger("generate_sbom")

SPDX_VERSION = "SPDX-2.3"
DATA_LICENSE = "CC0-1.0"
GOOGLE_ORG = "Organization: Google LLC"
CREATOR_TOOL = "Tool: aemu-sbom-generator-1.0.0"
NOASSERTION = "NOASSERTION"
# go/spdx-style-guide requires versionInfo on every package; this value marks
# versions that could not be discovered.
UNSPECIFIED_VERSION = "unspecified"
# Reproducible timestamp for hermetic Bazel builds (matches packaging.bzl epoch / RFC3339)
DEFAULT_CREATED_TIMESTAMP = "2026-01-01T00:00:00Z"

# Read limits, in bytes, for metadata files and license texts.
_MAX_METADATA_BYTES = 256 * 1024
_MAX_LICENSE_BYTES = 1024 * 1024

# File that marks the root of the emulator source workspace.
_WORKSPACE_MARKER = os.path.join("build", "bazel", "toplevel.MODULE.bazel")

# Workspace registry versions of the form 0.0.x denote unversioned local overlays.
_PLACEHOLDER_VERSION_RE = re.compile(r"0\.0\.[0-9]+")
# Values used by metadata files to say that the version is unknown.
_UNKNOWN_VERSION_VALUES = frozenset({"0", "n/a", "none", "unknown"})

# A license_kind is an SPDX License List identifier when it is one of the kinds that
# rules_license defines for every identifier (@rules_license//licenses/spdx:<ID>) or
# when it is named SPDX-license-identifier-<ID> (the AOSP convention).
_SPDX_KIND_PREFIX = "SPDX-license-identifier-"
_RULES_LICENSE_SPDX_KIND_RE = re.compile(
    r"@{0,2}rules_license(?:[+~][^/]*)?//licenses/spdx:([^:/]+)"
)
_SPDX_LICENSE_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+-]*")

# Source hosts and GitHub organizations whose packages are published by Google.
_GOOGLE_SOURCE_HOSTS = frozenset(
    {
        "android.googlesource.com",
        "boringssl.googlesource.com",
        "chromium.googlesource.com",
        "webrtc.googlesource.com",
    }
)
_GOOGLE_GITHUB_ORGS = frozenset({"google", "protocolbuffers", "webmproject"})
_GOOGLE_COPYRIGHT_RE = re.compile(
    r"Copyright.*(?:The Android Open Source Project|Google,?\s+(?:Inc|LLC))", re.IGNORECASE
)
# Number of leading copyright and license text lines searched for a Google copyright.
_SUPPLIER_SCAN_LINES = 100

# Bzlmod modules that contain first-party Android Emulator code.
_FIRST_PARTY_MODULES = frozenset({"aemu", "gfxstream", "goldfish", "netsim"})
# Top-level package directories that contain vendored third-party code.
_THIRD_PARTY_DIRS = frozenset({"external", "prebuilts", "third_party"})
# Canonical name of a module repository: the module name, optionally followed by one '+'
# or '~' separator and a version (protobuf+, protobuf~3.21.12). Repositories created by
# module extensions or repo rules (e.g. goldfish_crates++lrc+bitflags) do not match.
_MODULE_REPO_RE = re.compile(r"([A-Za-z0-9_.-]+?)(?:[+~][^+~]*)?")


# ---------------------------------------------------------------------------
# Generic helpers
# ---------------------------------------------------------------------------


def _as_str(val: Any) -> str:
    """Returns `val` without surrounding whitespace if it is a string, otherwise ''."""
    return val.strip() if isinstance(val, str) else ""


def _read_text(path: str, limit: int) -> str:
    """Reads up to `limit` bytes of a UTF-8 text file, ignoring a byte order mark.

    Logs a warning and returns '' if the file cannot be read. Also logs a warning
    when the file is truncated or is not valid UTF-8 (undecodable bytes are replaced).
    """
    try:
        with open(path, "rb") as f:
            data = f.read(limit + 1)
    except OSError as exc:
        _LOG.warning("cannot read %s: %s", path, exc)
        return ""
    truncated = len(data) > limit
    if truncated:
        _LOG.warning("%s is larger than %d bytes; only its beginning is used", path, limit)
        data = data[:limit]
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        # A multi-byte character cut by the truncation is not an encoding problem of the file.
        if not (truncated and exc.start >= len(data) - 3):
            _LOG.warning(
                "%s is not valid UTF-8 (%s); undecodable bytes were replaced", path, exc.reason
            )
        return data.decode("utf-8-sig", errors="replace")


def _strip_comment_lines(text: str) -> str:
    """Blanks out full-line '#' comments (Starlark, Meson, CMake, textproto)."""
    return re.sub(r"(?m)^[ \t]*#.*$", "", text)


def _call_arguments(text: str, open_paren: int) -> str:
    """Returns the arguments of the call whose '(' is at `open_paren` ('' if it never closes).

    Quoted strings, nested brackets, and trailing '#' comments are honored.
    """
    depth = 0
    quote_char = ""
    i = open_paren
    while i < len(text):
        char = text[i]
        if quote_char:
            if char == "\\":
                i += 1
            elif char == quote_char:
                quote_char = ""
        elif char in "'\"":
            quote_char = char
        elif char == "#":
            newline = text.find("\n", i)
            i = len(text) if newline == -1 else newline
            continue
        elif char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1 : i]
        i += 1
    return ""


def _keyword_string(arguments: str, name: str, separator: str = "=") -> str:
    """Returns the quoted string value of keyword argument `name` (e.g. `name = "x"`)."""
    m = re.search(
        rf"\b{re.escape(name)}\s*{re.escape(separator)}\s*(['\"])(.*?)\1", arguments, re.DOTALL
    )
    return m.group(2).strip() if m else ""


# ---------------------------------------------------------------------------
# Versions, labels, and identifiers
# ---------------------------------------------------------------------------


def _clean_version(raw_ver: Any) -> str:
    """Normalizes a version string by stripping tag prefixes and Bzlmod/AEMU suffixes."""
    ver = _as_str(raw_ver)
    if not ver:
        return ""
    ver = re.sub(r"^[A-Za-z_-]+-[vV]?(?=[0-9]+\.[0-9]+)", "", ver)
    ver = re.sub(r"^[vV](?=[0-9])", "", ver)
    return ver.split(".bcr.")[0].removesuffix(".aemu")


def _is_commit_hash(ver: str) -> bool:
    """Returns True for a full or abbreviated (7 to 40 hex digits) Git commit hash."""
    return bool(re.fullmatch(r"[0-9a-fA-F]{7,40}", ver) and re.search(r"[a-fA-F]", ver))


def _is_placeholder_version(ver: str) -> bool:
    """Returns True for 0.0.x versions, which denote unversioned local registry overlays."""
    return bool(_PLACEHOLDER_VERSION_RE.fullmatch(ver))


def _version_sort_key(ver: str) -> Tuple[List[Tuple[int, Any]], int, List[Tuple[int, Any]]]:
    """Orders versions the way Bazel does.

    Numeric components compare as numbers (1.10 > 1.9), a pre-release (2.0-rc1)
    sorts before its release, and build metadata (+build) is ignored.
    """

    def components(text: str) -> List[Tuple[int, Any]]:
        return [
            (0, int(part)) if re.fullmatch(r"[0-9]+", part) else (1, part)
            for part in re.split(r"[._-]", text)
            if part
        ]

    release, _, prerelease = ver.split("+", 1)[0].partition("-")
    return components(release), (0 if prerelease else 1), components(prerelease)


def _highest_raw_version(raw_versions: Iterable[str], skip_placeholders: bool = True) -> str:
    """Returns the highest raw version, optionally ignoring 0.0.x placeholders ('' if none).

    Raw versions are compared before cleaning so that Bzlmod revisions such as
    1.3.2.bcr.10 and 1.3.2.bcr.9 are ordered correctly.
    """
    usable = [
        raw
        for raw in raw_versions
        if _clean_version(raw)
        and not (skip_placeholders and _is_placeholder_version(_clean_version(raw)))
    ]
    return max(usable, key=_version_sort_key) if usable else ""


def _parse_label(label: Any) -> Optional[Tuple[str, str]]:
    """Splits a label such as '@@repo+//pkg/path:name' into (repository, package path)."""
    if not isinstance(label, str) or "//" not in label:
        return None
    repo, rest = label.split("//", 1)
    if repo and not repo.startswith("@"):
        return None
    return repo.lstrip("@"), rest.split(":", 1)[0]


def _repo_name_from_label(label: Any) -> str:
    """Returns the Bzlmod module whose repository holds `label` (@@protobuf+//x:y -> protobuf).

    Returns '' for the main repository and for repositories created by module
    extensions or repo rules, which are not modules themselves.
    """
    parsed = _parse_label(label)
    if not parsed or not parsed[0]:
        return ""
    m = _MODULE_REPO_RE.fullmatch(parsed[0])
    return m.group(1).lower() if m else ""


def _is_root_package(label: Any) -> bool:
    """Returns True if `label` is in the root package of its repository (e.g. @@zlib+//:license)."""
    parsed = _parse_label(label)
    return bool(parsed) and parsed[1] == ""


def _is_first_party_label(label: Any) -> bool:
    """Returns True if the Bazel label belongs to first-party Android Emulator code.

    That is the main repository or a first-party module repository, outside of
    their third_party/, external/, and prebuilts/ directories.
    """
    parsed = _parse_label(label)
    if not parsed:
        return False
    repo, package = parsed
    if package.split("/", 1)[0] in _THIRD_PARTY_DIRS:
        return False
    return not repo or _repo_name_from_label(label) in _FIRST_PARTY_MODULES


def _clean_repo_and_pkg_name(label: Any, raw_pkg_name: Any) -> str:
    """Normalizes a Bazel label or raw package_name into a clean component name."""
    if not isinstance(label, str):
        label = ""
    if isinstance(raw_pkg_name, str) and raw_pkg_name.strip():
        cleaned_raw = raw_pkg_name.strip().replace("\\", "/")
        if cleaned_raw.startswith("@") or "//" in cleaned_raw:
            name = cleaned_raw.split(":")[0].split("/")[-1].rstrip("+~")
        else:
            name = cleaned_raw.split("/")[-1]
        if name:
            return name

    # Rust crates use both Bazel 7 (~) and Bazel 8 (+) Bzlmod separators, e.g.
    # @@goldfish_crates++lrc+bitflags//:license or @@goldfish_crates~~lrc~bitflags//:license
    crate_match = re.match(r"^@@?[^/]+[+~]lrc[+~]([A-Za-z0-9_-]+)//", label)
    if crate_match:
        return f"rust-crate-{crate_match.group(1)}"

    if label.startswith("@"):
        repo = label.lstrip("@").split("//")[0].split("+")[0].split("~")[0]
        if repo:
            return repo

    if ":" in label or "/" in label:
        pkg_path = label.split(":")[0].lstrip("@/")
        parts = [
            p.rstrip("+~")
            for p in pkg_path.split("/")
            if p.rstrip("+~")
            and p.rstrip("+~") not in ("third_party", "hardware", "generic", "external")
        ]
        if parts:
            return "-".join(parts[-2:]) if len(parts) >= 2 else parts[0]

    cleaned_fallback = label.split(":")[0].lstrip("@/").rstrip("+~")
    return cleaned_fallback or "unknown"


def _make_spdx_id(prefix: str, key: str) -> str:
    """Creates a valid SPDXRef-* identifier with a deterministic 16-hex SHA-256 suffix."""
    sanitized = re.sub(r"[^A-Za-z0-9.-]", "-", prefix).strip("-") or "Pkg"
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()[:16]
    return f"SPDXRef-{sanitized}-{digest}"


def _make_license_ref_id(pkg_name: str, key: str) -> str:
    """Creates a valid LicenseRef-* identifier."""
    sanitized = re.sub(r"[^A-Za-z0-9.-]", "-", pkg_name).strip("-") or "Custom"
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()[:16]
    return f"LicenseRef-{sanitized}-{digest}"


# ---------------------------------------------------------------------------
# Workspace Bzlmod metadata
# ---------------------------------------------------------------------------


def _parse_lockfile(lockfile_path: Optional[str], warn_if_missing: bool = False) -> Dict[str, str]:
    """Returns the module versions that Bazel selected, from the source.json keys of a lockfile."""
    if not lockfile_path or not os.path.isfile(lockfile_path):
        if lockfile_path and warn_if_missing:
            _LOG.warning("module lockfile not found: %s", lockfile_path)
        return {}
    # The lockfile is a Bazel-generated build input and is parsed whole.
    try:
        with open(lockfile_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as exc:  # ValueError covers JSON and UTF-8 decode errors.
        _LOG.warning("cannot parse module lockfile %s: %s", lockfile_path, exc)
        return {}
    if not isinstance(data, dict):
        _LOG.warning("module lockfile %s is not a JSON object", lockfile_path)
        return {}
    registry_files = data.get("registryFileHashes")
    if not isinstance(registry_files, dict):
        _LOG.warning("module lockfile %s has no 'registryFileHashes' object", lockfile_path)
        return {}
    candidates: Dict[str, List[str]] = {}
    for url in registry_files:  # JSON object keys are always strings.
        m = re.search(r"/modules/([^/]+)/([^/]+)/source\.json$", url.replace("\\", "/"))
        if m:
            candidates.setdefault(m.group(1).lower(), []).append(m.group(2))
    # Registry releases may legitimately be numbered 0.0.x, so placeholders are kept here.
    versions = {
        module: _clean_version(_highest_raw_version(raw, skip_placeholders=False))
        for module, raw in candidates.items()
    }
    return {module: ver for module, ver in versions.items() if ver}


def _find_workspace_root(candidate_paths: Iterable[str]) -> Optional[str]:
    """Locates the workspace root by walking up from the resolved targets of input files."""
    for path in candidate_paths:
        if not path or not os.path.exists(path):
            continue
        cur = os.path.dirname(os.path.realpath(path))
        while True:
            if os.path.isfile(os.path.join(cur, _WORKSPACE_MARKER)):
                return cur
            parent = os.path.dirname(cur)
            if parent == cur:
                break
            cur = parent
    return None


def _parse_bazel_deps(module_file: str) -> Dict[str, str]:
    """Returns {module: version} for the non-dev bazel_dep() calls of a MODULE.bazel file."""
    text = _strip_comment_lines(_read_text(module_file, _MAX_METADATA_BYTES))
    deps: Dict[str, str] = {}
    for m in re.finditer(r"\bbazel_dep\s*\(", text):
        arguments = _call_arguments(text, m.end() - 1)
        if re.search(r"\bdev_dependency\s*=\s*True\b", arguments):
            continue
        name = _keyword_string(arguments, "name").lower()
        ver = _clean_version(_keyword_string(arguments, "version"))
        if name and ver and not _is_placeholder_version(ver):
            deps[name] = ver
    return deps


def _root_module_files(workspace_root: str) -> List[str]:
    """Returns the root MODULE.bazel file followed by the files it include()s."""
    root = os.path.join(workspace_root, _WORKSPACE_MARKER)
    files = [root]
    text = _strip_comment_lines(_read_text(root, _MAX_METADATA_BYTES))
    for m in re.finditer(r"\binclude\s*\(\s*(['\"])//([^:'\"]*):([^'\"]+)\1\s*\)", text):
        included = os.path.join(workspace_root, *m.group(2).split("/"), m.group(3))
        if os.path.isfile(included):
            files.append(included)
        else:
            _LOG.warning("module file included by %s not found: %s", root, included)
    return files


def _source_json_url(source_json: str) -> str:
    """Returns the download location recorded in a registry source.json ('' for local_path)."""
    try:
        data = json.loads(_read_text(source_json, _MAX_METADATA_BYTES))
    except ValueError as exc:
        _LOG.warning("cannot parse registry file %s: %s", source_json, exc)
        return ""
    if not isinstance(data, dict):
        _LOG.warning("registry file %s is not a JSON object", source_json)
        return ""
    source_type = _as_str(data.get("type")) or "archive"
    if source_type == "archive":
        url = _as_str(data.get("url"))
        if not url and isinstance(data.get("urls"), list):
            url = next((u.strip() for u in data["urls"] if isinstance(u, str) and u.strip()), "")
        return url if url.startswith(("https://", "http://")) else ""
    if source_type == "git_repository":
        remote = _as_str(data.get("remote"))
        ref = _as_str(data.get("commit")) or _as_str(data.get("tag"))
        if not remote.startswith(("https://", "http://")):
            return ""
        # SPDX VCS location syntax: git+<remote>[@<revision>].
        return f"git+{remote}@{ref}" if ref else f"git+{remote}"
    return ""


def discover_workspace_registry_metadata(
    workspace_root: Optional[str],
    lockfile_path: Optional[str] = None,
) -> Tuple[Dict[str, str], Dict[str, str]]:
    """Discovers module versions and source archive URLs from the workspace Bzlmod metadata.

    Versions come from three layers, each overriding the previous one:
      1. versions present in the local registry (the highest one per module);
      2. bazel_dep() versions requested by the root module and the files it includes;
      3. versions that Bazel selected, as recorded in the lockfile (`lockfile_path`
         if given, else the workspace MODULE.bazel.lock).
    A module's URL comes from the local registry entry of exactly the version
    reported for it, so the version and the URL always describe the same release.
    """
    versions: Dict[str, str] = {}
    local_sources: Dict[str, Dict[str, str]] = {}  # module -> {version directory: source.json}
    has_workspace = bool(workspace_root) and os.path.isdir(workspace_root)
    if has_workspace:
        modules_dir = os.path.join(workspace_root, "build", "bazel", "registry", "modules")
        if not os.path.isdir(modules_dir):
            _LOG.warning("workspace registry not found: %s", modules_dir)
        pattern = os.path.join(glob.escape(modules_dir), "*", "*", "source.json")
        for source_json in sorted(glob.glob(pattern)):
            version_dir = os.path.dirname(source_json)
            module = os.path.basename(os.path.dirname(version_dir)).lower()
            local_sources.setdefault(module, {})[os.path.basename(version_dir)] = source_json
        for module, sources in local_sources.items():
            raw = _highest_raw_version(sources)
            if raw:
                versions[module] = _clean_version(raw)
        for module_file in _root_module_files(workspace_root):
            versions.update(_parse_bazel_deps(module_file))

    if not lockfile_path and has_workspace:
        lockfile_path = os.path.join(workspace_root, "MODULE.bazel.lock")
    if lockfile_path:
        versions.update(_parse_lockfile(lockfile_path, warn_if_missing=True))

    urls: Dict[str, str] = {}
    for module, sources in local_sources.items():
        matching = [raw for raw in sources if _clean_version(raw) == versions.get(module)]
        if matching:
            url = _source_json_url(sources[max(matching, key=_version_sort_key)])
            if url:
                urls[module] = url
    return versions, urls


# ---------------------------------------------------------------------------
# Package-local metadata files
# ---------------------------------------------------------------------------


def _parse_version_file(path: str) -> Tuple[str, str]:
    """Reads the dotted version number that starts a VERSION or _VERSION file."""
    lines = _read_text(path, 1024).splitlines()
    m = re.match(r"[0-9]+\.[0-9]+[0-9A-Za-z.+-]*", lines[0].strip()) if lines else None
    return (_clean_version(m.group(0)) if m else ""), ""


def _parse_cargo_toml(path: str) -> Tuple[str, str]:
    """Reads version and repository (or homepage) URL from the [package] table of Cargo.toml."""
    text = _read_text(path, _MAX_METADATA_BYTES)
    header = re.search(r"^[ \t]*\[[ \t]*package[ \t]*\][ \t]*(?:#.*)?$", text, re.MULTILINE)
    if not header:
        return "", ""
    table = text[header.end() :]
    next_table = re.search(r"^[ \t]*\[", table, re.MULTILINE)
    if next_table:
        table = table[: next_table.start()]

    def value(key: str) -> str:
        m = re.search(rf"^[ \t]*{key}[ \t]*=[ \t]*(['\"])(.*?)\1", table, re.MULTILINE)
        return m.group(2).strip() if m else ""

    url = value("repository") or value("homepage")
    if not url.startswith(("https://", "http://")):
        url = ""
    return _clean_version(value("version")), url


def _parse_meson_build(path: str) -> Tuple[str, str]:
    """Reads the `version:` keyword of the project() call of a meson.build file."""
    text = _strip_comment_lines(_read_text(path, _MAX_METADATA_BYTES))
    m = re.search(r"\bproject\s*\(", text)
    if not m:
        return "", ""
    return _clean_version(_keyword_string(_call_arguments(text, m.end() - 1), "version", ":")), ""


def _parse_metadata_file(path: str) -> Tuple[str, str]:
    """Reads the version and source location (or homepage) of an AOSP METADATA textproto."""
    text = _strip_comment_lines(_read_text(path, _MAX_METADATA_BYTES))

    def field_value(name: str, value_pattern: str = r'[^"]+') -> str:
        # Fields may appear on their own line or inline (e.g. `identifier { value: "..." }`);
        # the lookbehind keeps `version` from matching inside `closest_version`.
        m = re.search(rf'(?<![\w.]){name}:\s*"({value_pattern})"', text)
        return m.group(1).strip() if m else ""

    raw_ver = field_value("version")
    raw_closest = field_value("closest_version")
    ver_known = raw_ver.lower() not in _UNKNOWN_VERSION_VALUES
    closest_known = raw_closest.lower() not in _UNKNOWN_VERSION_VALUES
    # Prefer closest_version when version is missing, unknown, or a raw Git commit hash.
    if raw_closest and closest_known and (not raw_ver or not ver_known or _is_commit_hash(raw_ver)):
        ver = _clean_version(raw_closest)
    elif raw_ver and ver_known:
        ver = _clean_version(raw_ver)
    else:
        ver = ""
    # The identifier/url value is where the code was obtained; homepage is a fallback.
    http_url = r'https?://[^"]+'
    return ver, field_value("value", http_url) or field_value("homepage", http_url)


def _parse_readme_metadata(path: str) -> Tuple[str, str]:
    """Reads the `Version:`, `Revision:`, and `URL:` fields of README.chromium-style files."""
    text = _read_text(path, _MAX_METADATA_BYTES)
    ver = ""
    m_ver = re.search(r"^Version:[ \t]*([^\r\n(]+)", text, re.MULTILINE | re.IGNORECASE)
    value = m_ver.group(1).strip() if m_ver else ""
    if value.lower() not in _UNKNOWN_VERSION_VALUES and re.search(r"[0-9]", value):
        ver = _clean_version(value)
    if not ver:
        m_rev = re.search(
            r"^Revision:[ \t]*([0-9a-fA-F]{7,40})\b", text, re.MULTILINE | re.IGNORECASE
        )
        ver = m_rev.group(1) if m_rev else ""
    m_url = re.search(r"^URL:[ \t]*(https?://\S+)", text, re.MULTILINE | re.IGNORECASE)
    return ver, (m_url.group(1).rstrip(".,;") if m_url else "")


def _parse_curlver_header(path: str) -> Tuple[str, str]:
    """Reads LIBCURL_VERSION from curl's curlver.h."""
    text = _read_text(path, _MAX_METADATA_BYTES)
    m = re.search(r'^\s*#\s*define\s+LIBCURL_VERSION\s+"([^"]+)"', text, re.MULTILINE)
    return (_clean_version(m.group(1)) if m else ""), ""


def _parse_module_bazel(path: str) -> Tuple[str, str]:
    """Reads the version argument of the module() call of a MODULE.bazel file."""
    text = _strip_comment_lines(_read_text(path, _MAX_METADATA_BYTES))
    m = re.search(r"^[ \t]*module\s*\(", text, re.MULTILINE)
    if not m:
        return "", ""
    ver = _clean_version(_keyword_string(_call_arguments(text, m.end() - 1), "version"))
    return ("" if _is_placeholder_version(ver) else ver), ""


def _parse_cmake_lists(path: str) -> Tuple[str, str]:
    """Reads the HOMEPAGE_URL argument of a CMake project() call."""
    text = _strip_comment_lines(_read_text(path, _MAX_METADATA_BYTES))
    m = re.search(r'\bHOMEPAGE_URL\s+"(https?://[^"]+)"', text)
    return "", (m.group(1).strip() if m else "")


# Metadata files inspected in each package directory, in priority order. Each
# parser returns a (version, url) pair with empty strings for unknown values.
_METADATA_SOURCES: Tuple[Tuple[str, Callable[[str], Tuple[str, str]]], ...] = (
    ("_VERSION", _parse_version_file),
    ("VERSION", _parse_version_file),
    ("Cargo.toml", _parse_cargo_toml),
    # netsim keeps its crate manifest below the repository root.
    (os.path.join("rust", "daemon", "Cargo.toml"), _parse_cargo_toml),
    ("meson.build", _parse_meson_build),
    ("METADATA", _parse_metadata_file),
    ("README.chromium", _parse_readme_metadata),
    ("README.crashpad", _parse_readme_metadata),
    ("README.breakpad", _parse_readme_metadata),
    ("README.android", _parse_readme_metadata),
    ("README.version", _parse_readme_metadata),
    ("curlver.h", _parse_curlver_header),
    ("MODULE.bazel", _parse_module_bazel),
    ("CMakeLists.txt", _parse_cmake_lists),
)


def _metadata_dirs(license_path: Optional[str], package_dir: Optional[str]) -> List[str]:
    """Lists the directories searched for a license file's metadata, innermost first.

    The search starts at the license file's directory and stops at the directory
    of the Bazel package that declares the license() target (`package_dir`,
    relative to the execution root like `license_path`; '' is the main repository's
    root package). Both paths are compared after resolving symlinks. If there is no
    license file, or it lies outside its package (e.g. a shared LICENSE file), only
    the package directory is searched; without a package directory, only the license
    file's.
    """
    has_license = (
        isinstance(license_path, str) and bool(license_path) and os.path.isfile(license_path)
    )
    if package_dir is None:
        return [os.path.dirname(os.path.realpath(license_path))] if has_license else []
    real_package_dir = os.path.realpath(package_dir or os.curdir)
    if not has_license:
        return [real_package_dir]
    license_dir = os.path.dirname(os.path.realpath(license_path))
    try:
        inside = os.path.commonpath([license_dir, real_package_dir]) == real_package_dir
    except ValueError:  # The paths are on different drives.
        inside = False
    if not inside:
        return [real_package_dir]
    dirs = [license_dir]
    while dirs[-1] != real_package_dir:
        dirs.append(os.path.dirname(dirs[-1]))
    return dirs


def discover_package_metadata_from_tree(
    license_path: Optional[str],
    package_dir: Optional[str] = None,
) -> Tuple[str, str]:
    """Discovers (version, url) from structured metadata files in a license's Bazel package.

    The closest value wins, except that a release version anywhere in the
    package is preferred over a Git commit hash.
    """
    ver = commit = url = ""
    for directory in _metadata_dirs(license_path, package_dir):
        for relative_path, parser in _METADATA_SOURCES:
            path = os.path.join(directory, relative_path)
            if not os.path.isfile(path):
                continue
            found_ver, found_url = parser(path)
            if found_ver and _is_commit_hash(found_ver):
                commit = commit or found_ver
            elif found_ver:
                ver = ver or found_ver
            url = url or found_url
    return ver or commit, url


# ---------------------------------------------------------------------------
# PURLs, suppliers, and licenses
# ---------------------------------------------------------------------------


def _url_host_and_segments(url: str) -> Tuple[str, List[str]]:
    """Returns the lowercase host and the non-empty path segments of a URL."""
    try:
        parts = urlsplit(url)
        host = (parts.hostname or "").lower()
    except ValueError:
        return "", []
    return host, [segment for segment in parts.path.split("/") if segment]


def _purl_quote(text: str) -> str:
    """Percent-encodes a purl component."""
    return quote(text, safe="")


def _infer_purl(pkg_name: str, pkg_ver: str, download_url: str) -> str:
    """Builds a Package URL (purl-spec) from the package name, version, and download location."""
    has_ver = bool(pkg_ver) and pkg_ver != UNSPECIFIED_VERSION
    ver_suffix = f"@{_purl_quote(pkg_ver)}" if has_ver else ""
    if pkg_name.startswith("rust-crate-"):
        return f"pkg:cargo/{_purl_quote(pkg_name.removeprefix('rust-crate-'))}{ver_suffix}"
    host, segments = _url_host_and_segments(download_url)
    if host == "github.com" and len(segments) >= 2:
        # GitHub owners and repository names are case-insensitive and lowercased in purls.
        owner = segments[0].lower()
        repo = segments[1].lower().removesuffix(".git")
        return f"pkg:github/{_purl_quote(owner)}/{_purl_quote(repo)}{ver_suffix}"
    if host == "android.googlesource.com":
        return f"pkg:generic/android/{_purl_quote(pkg_name)}{ver_suffix}"
    return f"pkg:generic/{_purl_quote(pkg_name)}{ver_suffix}"


def _purl_with_version(purl: str, pkg_ver: str) -> str:
    """Adds `@version` to a purl without one, ahead of any ?qualifiers or #subpath."""
    if not pkg_ver or pkg_ver == UNSPECIFIED_VERSION:
        return purl
    cut = min((i for i in (purl.find("?"), purl.find("#")) if i != -1), default=len(purl))
    base, suffix = purl[:cut], purl[cut:]
    if "@" in base:
        return purl
    return f"{base}@{_purl_quote(pkg_ver)}{suffix}"


def _infer_supplier(
    label: str,
    download_url: str,
    copyright_notice: str,
    license_text: str,
) -> str:
    """Returns Google LLC or NOASSERTION as the package supplier, per go/spdx-style-guide."""
    host, segments = _url_host_and_segments(download_url)
    if host in _GOOGLE_SOURCE_HOSTS:
        return GOOGLE_ORG
    if host == "github.com" and segments and segments[0].lower() in _GOOGLE_GITHUB_ORGS:
        return GOOGLE_ORG
    if _is_first_party_label(label):
        return GOOGLE_ORG
    head = "\n".join(f"{copyright_notice}\n{license_text}".splitlines()[:_SUPPLIER_SCAN_LINES])
    return GOOGLE_ORG if _GOOGLE_COPYRIGHT_RE.search(head) else NOASSERTION


def _spdx_license_id(kind_name: str, kind_label: str) -> str:
    """Returns the SPDX License List identifier of a license_kind, or '' if it is not one."""
    if kind_name.startswith(_SPDX_KIND_PREFIX):
        candidate = kind_name[len(_SPDX_KIND_PREFIX) :]
    else:
        m = _RULES_LICENSE_SPDX_KIND_RE.fullmatch(kind_label)
        candidate = m.group(1) if m else ""
    return candidate if _SPDX_LICENSE_ID_RE.fullmatch(candidate) else ""


def _read_license_text(path: Optional[str], fallback_license: str) -> str:
    """Returns the license text of a package, or a short statement when it has no readable text."""
    if path:
        if os.path.isfile(path):
            text = _read_text(path, _MAX_LICENSE_BYTES).strip()
            if text:
                return text
            _LOG.warning("license file %s is empty or unreadable", path)
        else:
            _LOG.warning("license file not found: %s", path)
    if fallback_license == NOASSERTION:
        return "License information for this component could not be asserted."
    if " " in fallback_license:
        return f"This component is licensed under {fallback_license}."
    return (
        f"This component is licensed under {fallback_license}. "
        f"See https://spdx.org/licenses/{fallback_license}.html"
    )


# ---------------------------------------------------------------------------
# SPDX document
# ---------------------------------------------------------------------------


@dataclass
class LicenseEntry:
    """Metadata of one license() target, which becomes one SPDX package."""

    label: str
    package_name: str
    package_url: str = ""
    package_version: str = ""
    copyright_notice: str = ""
    # (name, label) of each license_kind.
    license_kinds: List[Tuple[str, str]] = field(default_factory=list)
    license_text_path: Optional[str] = None
    # Execution-root-relative directory of the Bazel package declaring the license.
    package_dir: Optional[str] = None


def _collect_reports(
    reports: Any,
) -> Tuple[Dict[str, Any], List[Dict[str, Any]], List[Dict[str, Any]]]:
    """Returns (product metadata, license records, package records) from all report objects.

    Malformed entries are skipped with a warning.
    """
    product_meta: Dict[str, Any] = {}
    licenses: List[Dict[str, Any]] = []
    packages: List[Dict[str, Any]] = []
    if not isinstance(reports, list):
        _LOG.warning("licenses_info is not a JSON list; no packages were read")
        return product_meta, licenses, packages
    for index, report in enumerate(reports):
        if not isinstance(report, dict):
            _LOG.warning("report %d is not a JSON object; skipped", index)
            continue
        meta = report.get("product_metadata")
        if isinstance(meta, dict):
            product_meta = product_meta or dict(meta)
        elif meta is not None:
            _LOG.warning("report %d: product_metadata is not a JSON object; ignored", index)
        for key, target in (("licenses_list", licenses), ("packages_list", packages)):
            records = report.get(key)
            if records is None:
                continue
            if not isinstance(records, list):
                _LOG.warning("report %d: %s is not a list; skipped", index, key)
                continue
            valid = [record for record in records if isinstance(record, dict)]
            if len(valid) != len(records):
                _LOG.warning(
                    "report %d: %d %s entries are not JSON objects; skipped",
                    index,
                    len(records) - len(valid),
                    key,
                )
            target.extend(valid)
    return product_meta, licenses, packages


def _license_kinds(lic: Dict[str, Any], label: str) -> List[Tuple[str, str]]:
    """Returns the (name, label) pairs of a license record's kinds, warning about bad entries."""
    raw_kinds = lic.get("license_kinds")
    if raw_kinds is None:
        return []
    if not isinstance(raw_kinds, list):
        _LOG.warning("%s: license_kinds is not a list; ignored", label)
        return []
    kinds = []
    for kind in raw_kinds:
        if isinstance(kind, dict) and (_as_str(kind.get("name")) or _as_str(kind.get("label"))):
            kinds.append((_as_str(kind.get("name")), _as_str(kind.get("label"))))
        else:
            _LOG.warning("%s: ignoring malformed license kind %r", label, kind)
    return kinds


def _license_entries(raw_licenses: List[Dict[str, Any]]) -> Dict[str, LicenseEntry]:
    """Groups license records by license() target label, so each target is one SPDX package.

    Distinct targets are never merged, even when they share a package name: two
    vendored copies of a library keep their own version, URL, and license text.
    """
    entries: Dict[str, LicenseEntry] = {}
    for index, lic in enumerate(raw_licenses):
        label = _as_str(lic.get("label"))
        if not label:
            _LOG.warning("license record %d has no label", index)
        raw_package_dir = lic.get("package_dir")
        record = LicenseEntry(
            label=label,
            package_name=_clean_repo_and_pkg_name(label, lic.get("package_name")),
            package_url=_as_str(lic.get("package_url")),
            package_version=_as_str(lic.get("package_version")),
            copyright_notice=_as_str(lic.get("copyright_notice")),
            license_kinds=_license_kinds(lic, label or f"license record {index}"),
            license_text_path=_as_str(lic.get("license_text")) or None,
            package_dir=raw_package_dir if isinstance(raw_package_dir, str) else None,
        )
        key = label or f"unlabeled-{index}"
        existing = entries.get(key)
        if existing is None:
            entries[key] = record
            continue
        for kind in record.license_kinds:
            if kind not in existing.license_kinds:
                existing.license_kinds.append(kind)
        existing.package_url = existing.package_url or record.package_url
        existing.package_version = existing.package_version or record.package_version
        existing.copyright_notice = existing.copyright_notice or record.copyright_notice
        if not existing.license_text_path:
            existing.license_text_path = record.license_text_path
            existing.package_dir = record.package_dir
    return entries


def _package_info_index(
    raw_packages: List[Dict[str, Any]],
) -> Tuple[Dict[str, Dict[str, Any]], Dict[str, Dict[str, Any]]]:
    """Indexes package_info records by Bazel package directory and by unambiguous name."""
    by_dir: Dict[str, Dict[str, Any]] = {}
    by_name: Dict[str, List[Dict[str, Any]]] = {}
    for pkg_info in raw_packages:
        package_dir = pkg_info.get("package_dir")
        if isinstance(package_dir, str):
            by_dir.setdefault(package_dir, pkg_info)
        name = _clean_repo_and_pkg_name(pkg_info.get("label"), pkg_info.get("package_name"))
        by_name.setdefault(name.lower(), []).append(pkg_info)
    ambiguous = sorted(name for name, infos in by_name.items() if len(infos) > 1)
    if ambiguous:
        _LOG.warning(
            "package_info names %s are ambiguous; they only apply to license() targets "
            "in the same Bazel package",
            ", ".join(ambiguous),
        )
    unique = {name: infos[0] for name, infos in by_name.items() if len(infos) == 1}
    return by_dir, unique


def build_spdx_document(
    reports: List[Dict[str, Any]],
    lockfile_path: Optional[str] = None,
    created_timestamp: str = DEFAULT_CREATED_TIMESTAMP,
    workspace_root: Optional[str] = None,
    extra_metadata_srcs: Optional[List[str]] = None,
) -> Dict[str, Any]:
    """Constructs a complete SPDX 2.3 JSON document from gathered Bazel metadata."""
    product_meta, raw_licenses, raw_packages = _collect_reports(reports)
    entries = _license_entries(raw_licenses)

    product_name = _as_str(product_meta.get("product_name")) or "emulator"
    platform_name = _as_str(product_meta.get("platform")) or "linux"
    build_id = _as_str(product_meta.get("build_id")) or "developer"
    version = _as_str(product_meta.get("version")) or _as_str(product_meta.get("revision"))
    if not version:
        _LOG.warning("product metadata has no version; reporting %r", UNSPECIFIED_VERSION)
        full_version = UNSPECIFIED_VERSION
    elif build_id != "developer":
        full_version = f"{version}-{build_id}"
    else:
        full_version = version
    doc_name = f"sdk-repo-{platform_name}-{product_name}-{build_id}"

    # Resolve module versions and archive URLs from the workspace Bzlmod metadata.
    extra_srcs = [src for src in extra_metadata_srcs or [] if isinstance(src, str) and src]
    candidate_paths = extra_srcs + [
        entry.license_text_path for entry in entries.values() if entry.license_text_path
    ]
    ws_root = workspace_root or _find_workspace_root(candidate_paths)
    if not ws_root and candidate_paths:
        _LOG.warning(
            "cannot locate the workspace root (%s) from the input files; registry "
            "versions and download URLs are unavailable",
            _WORKSPACE_MARKER,
        )
    module_versions, registry_urls = discover_workspace_registry_metadata(ws_root, lockfile_path)
    pkg_info_by_dir, pkg_info_by_name = _package_info_index(raw_packages)

    # Root package: SPDXRef-Package-main (required by go/spdx-style-guide)
    root_spdx_id = "SPDXRef-Package-main"
    root_purl = _purl_with_version(
        f"pkg:generic/google/android-emulator?platform={_purl_quote(platform_name)}", full_version
    )
    spdx_packages: List[Dict[str, Any]] = [
        {
            "name": doc_name,
            "SPDXID": root_spdx_id,
            "versionInfo": full_version,
            "supplier": GOOGLE_ORG,
            "downloadLocation": "https://developer.android.com/studio/releases/emulator",
            "filesAnalyzed": False,
            "licenseConcluded": "Apache-2.0",
            "licenseDeclared": "Apache-2.0",
            "copyrightText": "Copyright 2026 The Android Open Source Project",
            "summary": _as_str(product_meta.get("desc")) or "Android Emulator",
            "externalRefs": [
                {
                    "referenceCategory": "PACKAGE-MANAGER",
                    "referenceType": "purl",
                    "referenceLocator": root_purl,
                }
            ],
        }
    ]
    relationships: List[Dict[str, str]] = [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relatedSpdxElement": root_spdx_id,
            "relationshipType": "DESCRIBES",
        }
    ]
    extracted_licenses: Dict[str, Dict[str, Any]] = {}

    for key in sorted(entries, key=lambda k: (entries[k].package_name.lower(), k)):
        entry = entries[key]
        pkg_name = entry.package_name
        pkg_info = (
            pkg_info_by_dir.get(entry.package_dir) if entry.package_dir is not None else None
        ) or pkg_info_by_name.get(pkg_name.lower(), {})
        tree_ver, tree_url = discover_package_metadata_from_tree(
            entry.license_text_path, entry.package_dir
        )
        # Registry metadata describes a module's root package only; vendored
        # sub-packages are described solely by their own metadata.
        module = _repo_name_from_label(entry.label) if _is_root_package(entry.label) else ""

        pkg_ver = (
            _clean_version(entry.package_version)
            or _clean_version(pkg_info.get("package_version"))
            or tree_ver
            or (module_versions.get(module, "") if module else "")
            or (full_version if _is_first_party_label(entry.label) else "")
            or UNSPECIFIED_VERSION
        )

        is_crate = pkg_name.startswith("rust-crate-")
        crate_name = pkg_name.removeprefix("rust-crate-") if is_crate else ""
        crate_url = f"https://crates.io/crates/{_purl_quote(crate_name)}" if crate_name else ""
        if crate_url and pkg_ver != UNSPECIFIED_VERSION:
            crate_url = f"{crate_url}/{_purl_quote(pkg_ver)}"
        download_url = (
            entry.package_url
            or _as_str(pkg_info.get("package_url"))
            or tree_url
            or (registry_urls.get(module, "") if module else "")
            or crate_url
            or NOASSERTION
        )

        purl = _purl_with_version(
            _as_str(pkg_info.get("purl")) or _infer_purl(pkg_name, pkg_ver, download_url),
            pkg_ver,
        )

        std_ids: List[str] = []
        unknown_kinds: List[str] = []
        for kind_name, kind_label in entry.license_kinds:
            spdx_id = _spdx_license_id(kind_name, kind_label)
            if not spdx_id:
                unknown_kinds.append(kind_name or kind_label)
            elif spdx_id not in std_ids:
                std_ids.append(spdx_id)
        std_expr = " AND ".join(std_ids) if std_ids else NOASSERTION
        lic_ref_id = _make_license_ref_id(pkg_name, entry.license_text_path or key)
        if unknown_kinds:
            _LOG.warning(
                "%s: license kinds %s are not SPDX License List identifiers; licenseDeclared "
                "references the extracted license text %s instead",
                entry.label or pkg_name,
                ", ".join(unknown_kinds),
                lic_ref_id,
            )
        needs_license_ref = bool(unknown_kinds) or not std_ids
        declared_expr = " AND ".join(std_ids + ([lic_ref_id] if needs_license_ref else []))
        lic_text = _read_license_text(entry.license_text_path, std_expr)
        supplier = _infer_supplier(
            label=entry.label,
            download_url=download_url,
            copyright_notice=entry.copyright_notice,
            license_text=lic_text,
        )

        if lic_ref_id not in extracted_licenses:
            if std_ids:
                cross_refs = [f"https://spdx.org/licenses/{sid}.html" for sid in std_ids]
            elif download_url != NOASSERTION:
                cross_refs = [download_url]
            else:
                cross_refs = ["https://spdx.org/licenses/"]
            extracted_licenses[lic_ref_id] = {
                "licenseId": lic_ref_id,
                "name": std_expr if std_ids else f"{pkg_name}-license",
                "extractedText": lic_text,
                "seeAlsos": cross_refs,
            }

        pkg_spdx_id = _make_spdx_id(f"Package-{pkg_name}", key)
        pkg_dict: Dict[str, Any] = {
            "name": pkg_name,
            "SPDXID": pkg_spdx_id,
            "versionInfo": pkg_ver,
            "supplier": supplier,
            "downloadLocation": download_url,
            "filesAnalyzed": False,
            "licenseConcluded": lic_ref_id,
            "licenseDeclared": declared_expr,
            "copyrightText": entry.copyright_notice or NOASSERTION,
            "externalRefs": [
                {
                    "referenceCategory": "PACKAGE-MANAGER",
                    "referenceType": "purl",
                    "referenceLocator": purl,
                }
            ],
        }
        if entry.label:
            pkg_dict["sourceInfo"] = f"Bazel license target {entry.label}"
        spdx_packages.append(pkg_dict)

        for relationship_type in ("CONTAINS", "DEPENDS_ON"):
            relationships.append(
                {
                    "spdxElementId": root_spdx_id,
                    "relatedSpdxElement": pkg_spdx_id,
                    "relationshipType": relationship_type,
                }
            )

    has_extracted_licenses = [extracted_licenses[k] for k in sorted(extracted_licenses)]
    # The namespace is deterministic but differs whenever the document content differs.
    content_digest = hashlib.sha256(
        json.dumps([spdx_packages, has_extracted_licenses], sort_keys=True).encode("utf-8")
    ).hexdigest()
    doc_uuid = uuid.uuid5(uuid.NAMESPACE_URL, f"{doc_name}:{full_version}:{content_digest}")

    return {
        "spdxVersion": SPDX_VERSION,
        "dataLicense": DATA_LICENSE,
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": doc_name,
        "documentNamespace": f"https://spdx.google/{doc_uuid}",
        "creationInfo": {
            "licenseListVersion": "3.25",
            "creators": [
                GOOGLE_ORG,
                CREATOR_TOOL,
            ],
            "created": created_timestamp,
        },
        "packages": spdx_packages,
        "hasExtractedLicensingInfos": has_extracted_licenses,
        "relationships": relationships,
    }


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an SPDX 2.3 JSON SBOM from a Bazel rules_license JSON report."
    )
    parser.add_argument(
        "--licenses_info",
        required=True,
        help="Path to the intermediate SBOM licenses JSON file.",
    )
    parser.add_argument(
        "--out",
        required=True,
        help="Path to the output .spdx.json file.",
    )
    parser.add_argument(
        "--module_lockfile",
        default=None,
        help="Optional MODULE.bazel.lock to use instead of the workspace lockfile.",
    )
    parser.add_argument(
        "--metadata_src",
        action="append",
        default=[],
        help="Workspace file used to locate the workspace root (repeatable).",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    try:
        with open(args.licenses_info, "r", encoding="utf-8") as f:
            reports = json.load(f)
    except (OSError, ValueError) as exc:  # ValueError covers JSON and UTF-8 decode errors.
        _LOG.error("cannot load licenses_info %s: %s", args.licenses_info, exc)
        return 1
    if not isinstance(reports, list):
        _LOG.error(
            "expected a JSON list in %s, got %s", args.licenses_info, type(reports).__name__
        )
        return 1
    for src in args.metadata_src:
        if not os.path.isfile(src):
            _LOG.warning("metadata source not found: %s", src)

    spdx_doc = build_spdx_document(
        reports,
        lockfile_path=args.module_lockfile,
        extra_metadata_srcs=args.metadata_src,
    )
    try:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as out_f:
            json.dump(spdx_doc, out_f, indent=2)
            out_f.write("\n")
    except OSError as exc:
        _LOG.error("cannot write SBOM to %s: %s", args.out, exc)
        return 1
    return 0


if __name__ == "__main__":
    logging.basicConfig(format="generate_sbom: %(levelname)s: %(message)s")
    sys.exit(main())
