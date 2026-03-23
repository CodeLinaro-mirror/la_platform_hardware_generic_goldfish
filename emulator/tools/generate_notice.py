# Copyright 2026 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import json
import logging
import os
import sys
from enum import IntEnum
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple


class ContentQuality(IntEnum):
    SPDX_FALLBACK = 1
    REAL_TEXT = 2


@dataclass
class LicenseContent:
    text: str
    quality: ContentQuality


@dataclass
class LicenseKind:
    name: str


@dataclass
class LicenseInfo:
    label: str
    package_name: Optional[str]
    package_url: Optional[str]
    package_version: Optional[str]
    copyright_notice: Optional[str]
    license_kinds: List[LicenseKind] = field(default_factory=list)
    license_text: Optional[str] = None


# Map of standard exact licenses that can be grouped across packages safely
STANDARD_LICENSES = {
    "Apache-2.0": "license-apache-2-0",
    "SPDX-license-identifier-Apache-2.0": "license-apache-2-0",
    "APSL-2.0": "license-apsl-2-0",
    "GPL-2.0-only": "license-gpl-2-0",
    "GPL-2.0-or-later": "license-gpl-2-0",
    "SPDX-license-identifier-GPL-2.0-only": "license-gpl-2-0",
    "LGPL-2.1-or-later": "license-lgpl-2-1",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a notice markdown file from a rules_license JSON report."
    )
    parser.add_argument(
        "--licenses_info", required=True, help="Path to the rules_license JSON file."
    )
    parser.add_argument(
        "--out", required=True, help="Path to the output notice.md file."
    )
    return parser.parse_args()


def load_license_data(filepath) -> List[LicenseInfo]:
    """
    Loads and parses the rules_license JSON file, returning strongly typed objects.

    Expected JSON structure:
    [
        {
            "licenses_list": [
                {
                    "label": "@@repo//pkg:license",
                    "package_name": "pkg",
                    "package_url": "https://example.com/pkg",
                    "package_version": "1.0",
                    "copyright_notice": "Copyright 2026",
                    "license_kinds": [{"name": "Apache-2.0"}],
                    "license_text": "external/repo+/pkg/LICENSE"
                }
            ]
        }
    ]
    """
    try:
        with open(filepath) as f:
            data = json.load(f)
            licenses = []
            for report in data:
                for lic in report.get("licenses_list", []):
                    kinds = [
                        LicenseKind(name=k.get("name"))
                        for k in lic.get("license_kinds", [])
                    ]
                    licenses.append(
                        LicenseInfo(
                            label=lic.get("label", "Unknown"),
                            package_name=lic.get("package_name"),
                            package_url=lic.get("package_url"),
                            package_version=lic.get("package_version"),
                            copyright_notice=lic.get("copyright_notice"),
                            license_kinds=kinds,
                            license_text=lic.get("license_text"),
                        )
                    )
            return licenses
    except Exception as e:
        logging.error("Failed to load %s: %s", filepath, e)
        sys.exit(1)


def _extract_package_name_from_label(label: str) -> str:
    """Derives a reasonable package name from a Bazel label."""
    if label.startswith("@"):
        # External dependency: '@repo_name//foo:bar' -> 'repo_name'
        return label.lstrip("@").split("//")[0]
    elif ":" in label:
        # Internal dependency: '//foo/bar/baz:qux' -> 'baz'
        return label.split(":")[0].split("/")[-1]

    return label


def extract_packages(licenses: List[LicenseInfo]) -> Dict[str, Dict[str, LicenseInfo]]:
    """Parses LicenseInfo objects and normalizes package names."""
    packages = {}
    for lic in licenses:
        label = lic.label
        pkg_name = lic.package_name

        # Fallback heuristics: If the license target did not explicitly define
        # a 'package_name' attribute, attempt to derive a reasonable name from
        # its Bazel label.
        if not pkg_name:
            pkg_name = _extract_package_name_from_label(label)

        # Strip any directory paths to get just the final package name
        pkg_name = pkg_name.split("/")[-1]

        if pkg_name not in packages:
            packages[pkg_name] = {}

        # Avoid duplicate licenses per package if they point to the same text file
        lic_text_path = lic.license_text
        if not lic_text_path:
            # If no text is provided, we use the label as a unique key for this license instance
            lic_text_path = f"virtual://{lic.label}"

        if lic_text_path not in packages[pkg_name]:
            packages[pkg_name][lic_text_path] = lic
    return packages


def _read_license_text(lic: LicenseInfo, lic_text_path: str) -> LicenseContent:
    """Reads license text from disk or falls back to an SPDX identifier."""
    if lic_text_path.startswith("virtual://") or not os.path.exists(lic_text_path):
        spdx_id = None
        kinds = lic.license_kinds
        if kinds:
            spdx_id = kinds[0].name

        if spdx_id:
            logging.info(
                "Text not found (%s), using fallback for %s",
                lic_text_path,
                spdx_id,
            )
            content = LicenseContent(
                text=f"This component is licensed under the {spdx_id} license.\n\nA full copy of the standard license text can be found at https://spdx.org/licenses/{spdx_id}.html",
                quality=ContentQuality.SPDX_FALLBACK,
            )
        else:
            logging.fatal("License text file not found: %s", lic_text_path)
            sys.exit(1)
    else:
        try:
            with open(lic_text_path, "r", encoding="utf-8") as f:
                content = LicenseContent(
                    text=f.read(),
                    quality=ContentQuality.REAL_TEXT,
                )
        except Exception as e:
            logging.fatal("Failed to read license text %s: %s", lic_text_path, e)
            sys.exit(1)

    content.text = content.text.strip()
    return content


def deduplicate_licenses(
    packages: Dict[str, Dict[str, LicenseInfo]],
) -> Tuple[Dict[str, List[Dict[str, Any]]], Dict[str, LicenseContent], Dict[str, str]]:
    """Reads license files, handles missing backups, and associates them into mapped groups."""
    package_to_licenses = {}

    # Map from exact license text content -> generated Group ID
    text_to_group = {}
    # Store the actual text for the appendix
    group_to_text = {}
    # Semantic human readable markdown label
    group_to_name = {}

    group_counter = 1

    for pkg_name in sorted(packages.keys()):
        package_to_licenses[pkg_name] = []
        for lic_text_path, lic in packages[pkg_name].items():
            content = _read_license_text(lic, lic_text_path)

            group_id = None
            kinds = lic.license_kinds
            for kind in kinds:
                name = kind.name
                if name in STANDARD_LICENSES:
                    group_id = STANDARD_LICENSES[name]
                    # Retain the highest fidelity text for standard licenses
                    if (
                        group_id not in group_to_text
                        or content.quality > group_to_text[group_id].quality
                    ):
                        group_to_text[group_id] = content
                        group_to_name[group_id] = group_id
                    break

            if not group_id:
                if content.text not in text_to_group:
                    group_id = f"license-group-{group_counter}"
                    text_to_group[content.text] = group_id
                    group_to_text[group_id] = content
                    group_to_name[group_id] = f"license-{pkg_name}"
                    group_counter += 1
                group_id = text_to_group[content.text]

            package_to_licenses[pkg_name].append(
                {"metadata": lic, "group_id": group_id}
            )

    return package_to_licenses, group_to_text, group_to_name


def _generate_anchor(pkg_name: str) -> str:
    """Generates a markdown-compatible anchor string from a package name."""
    return pkg_name.lower().replace(" ", "-").replace("/", "-").replace("+", "-")


def write_markdown_notice(
    out_path: str,
    packages: Dict[str, Dict[str, LicenseInfo]],
    package_to_licenses: Dict[str, List[Dict[str, Any]]],
    group_to_text: Dict[str, LicenseContent],
    group_to_name: Dict[str, str],
) -> None:
    """Outputs the assembled data into Notice Markdown file structure."""
    sections = [
        "# Open Source Software Notices\n",
        "This software contains components from the following open source projects:\n",
    ]

    # Generate Table of Contents
    toc = [
        f"- [{pkg_name}](#{_generate_anchor(pkg_name)})"
        for pkg_name in sorted(packages.keys())
    ]
    sections.extend(toc)
    sections.append("\n---\n")

    # Generate Package Details
    for pkg_name in sorted(packages.keys()):
        anchor = _generate_anchor(pkg_name)
        sections.append(f'<a id="{anchor}"></a>\n## {pkg_name}\n')

        for entry in package_to_licenses[pkg_name]:
            lic = entry["metadata"]
            group_id = entry["group_id"]
            group_name = group_to_name[group_id]

            lines = []
            if lic.package_version:
                lines.append(f"**Version:** {lic.package_version}  ")
            if lic.package_url:
                lines.append(f"**URL:** [{lic.package_url}]({lic.package_url})  ")
            if lic.copyright_notice:
                lines.append(f"**Copyright:** {lic.copyright_notice}  ")

            lines.append(f"**License:** See [{group_name}](#{group_id})\n")
            sections.append("\n".join(lines))

    # Generate License Appendix
    sections.append("\n---\n\n# Appendix of Licenses\n")
    for group_id, content in group_to_text.items():
        group_name = group_to_name[group_id]
        sections.append(
            f'<a id="{group_id}"></a>\n'
            f"## {group_name}\n\n"
            f"```\n{content.text}\n```\n"
        )

    with open(out_path, "w", encoding="utf-8") as out_f:
        out_f.write("\n".join(sections) + "\n")


def main() -> None:
    args = parse_args()
    logging.basicConfig(level=logging.INFO)

    data = load_license_data(args.licenses_info)
    packages = extract_packages(data)
    package_to_licenses, group_to_text, group_to_name = deduplicate_licenses(packages)
    write_markdown_notice(
        args.out, packages, package_to_licenses, group_to_text, group_to_name
    )


if __name__ == "__main__":
    main()
