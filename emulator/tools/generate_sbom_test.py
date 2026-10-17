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
"""Unit tests for generate_sbom.py."""

import json
import os
import re
import tempfile
import unittest
from unittest import mock

import generate_sbom


def _write(path, content):
    """Writes a text (str) or binary (bytes) fixture, creating parent directories."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if isinstance(content, bytes):
        with open(path, "wb") as f:
            f.write(content)
    else:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
    return path


def _spdx_kind(spdx_id):
    """Returns a license kind record for one of rules_license's SPDX kinds."""
    return {"name": spdx_id, "label": f"@rules_license+//licenses/spdx:{spdx_id}"}


def _report(licenses, version="37.1.5", build_id="16500000", packages=None):
    """Returns a single-report licenses_info payload."""
    return [
        {
            "top_level_target": "@@goldfish+//emulator:sbom",
            "product_metadata": {
                "product_name": "emulator",
                "platform": "linux",
                "version": version,
                "build_id": build_id,
                "desc": "Android Emulator",
            },
            "licenses_list": licenses,
            "packages_list": packages or [],
        }
    ]


def _license(label, license_text=None, package_dir=None, **attrs):
    """Returns a license record as emitted by sbom.bzl.

    Like rules_license, package_name defaults to the package path of the label.
    """
    record = {
        "label": label,
        "package_name": label.split("//", 1)[-1].split(":", 1)[0],
        "package_url": "",
        "package_version": "",
        "copyright_notice": "",
        "license_kinds": [_spdx_kind("BSD-3-Clause")],
        "license_text": license_text,
        "package_dir": package_dir,
    }
    record.update(attrs)
    return record


def _logged(logs, text):
    """Returns True if any captured log line contains `text`."""
    return any(text in line for line in logs.output)


class GenerateSbomTest(unittest.TestCase):

    def test_no_hardcoded_known_package_catalog(self):
        self.assertFalse(
            hasattr(generate_sbom, "KNOWN_PACKAGE_CATALOG"),
            "KNOWN_PACKAGE_CATALOG must not exist; versions and URLs must be discovered dynamically.",
        )

    def test_versions_and_labels_are_normalized(self):
        self.assertEqual(generate_sbom._clean_version("vendor"), "vendor")
        self.assertEqual(generate_sbom._clean_version("v1.4.354"), "1.4.354")
        self.assertEqual(generate_sbom._clean_version("libaom-v3.13.3"), "3.13.3")
        self.assertEqual(generate_sbom._clean_version("1.16.0.bcr.beta.1"), "1.16.0")
        self.assertEqual(generate_sbom._clean_version("21.1.8.aemu"), "21.1.8")
        self.assertEqual(generate_sbom._clean_version(None), "")
        self.assertEqual(generate_sbom._clean_version(3), "")
        self.assertEqual(
            generate_sbom._clean_repo_and_pkg_name("@@goldfish_crates~~lrc~bitflags//:license", ""),
            "rust-crate-bitflags",
        )
        self.assertEqual(
            generate_sbom._clean_repo_and_pkg_name("@@goldfish_crates++lrc+bitflags//:license", ""),
            "rust-crate-bitflags",
        )
        self.assertEqual(
            generate_sbom._clean_repo_and_pkg_name("@@libstdcpp+//:license", "libstdc++"),
            "libstdc++",
        )
        self.assertEqual(
            generate_sbom._clean_repo_and_pkg_name(
                "@@breakpad+//src/third_party/curl:license", "src/third_party/curl"
            ),
            "curl",
        )
        self.assertEqual(
            generate_sbom._clean_repo_and_pkg_name("//third_party/liba/common:license", ""),
            "liba-common",
        )
        self.assertEqual(generate_sbom._clean_repo_and_pkg_name("//:license", ""), "unknown")
        self.assertEqual(
            generate_sbom._repo_name_from_label("@@protobuf~3.21.12//third_party/utf8_range:license"),
            "protobuf",
        )
        self.assertEqual(
            generate_sbom._repo_name_from_label("@@protobuf+//third_party/utf8_range:license"),
            "protobuf",
        )
        # Repositories created by module extensions or repo rules are not modules.
        self.assertEqual(
            generate_sbom._repo_name_from_label("@@goldfish_crates++lrc+bitflags//:license"), ""
        )
        self.assertEqual(generate_sbom._repo_name_from_label("//emulator:license"), "")
        self.assertTrue(generate_sbom._is_root_package("@@zlib+//:license"))
        self.assertFalse(generate_sbom._is_root_package("@@protobuf+//third_party/x:license"))

    def test_first_party_labels(self):
        for label in (
            "//emulator/tools:license",
            "@//emulator:license",
            "@@//emulator:license",
            "@@goldfish+//emulator:license",
            "@goldfish//emulator/plugin/virtio-wifi:license",
        ):
            self.assertTrue(generate_sbom._is_first_party_label(label), label)
        for label in (
            "//third_party/zlib:license",
            "@@zlib+//:license",
            "@@goldfish+//third_party/foo:license",
            "@@goldfish_crates++lrc+bitflags//:license",
            "@@goldfish++python+prebuilt_python//:license",
            "not a label",
        ):
            self.assertFalse(generate_sbom._is_first_party_label(label), label)

    def test_versions_sort_like_bazel(self):
        key = generate_sbom._version_sort_key
        self.assertEqual(max(["1.9.0", "1.10.0", "1.2.0"], key=key), "1.10.0")
        self.assertEqual(max(["2.0.0-rc1", "2.0.0"], key=key), "2.0.0")
        self.assertEqual(key("1.2.3+build.7"), key("1.2.3"))
        self.assertEqual(
            generate_sbom._highest_raw_version(["1.3.2.bcr.9", "1.3.2.bcr.10", "0.0.3"]),
            "1.3.2.bcr.10",
        )
        self.assertEqual(generate_sbom._highest_raw_version(["0.0.1", ""]), "")
        self.assertEqual(
            generate_sbom._highest_raw_version(["0.0.11"], skip_placeholders=False), "0.0.11"
        )
        self.assertTrue(generate_sbom._is_commit_hash("ed31caa60f20a4f6569883b2d752ef7522de51e0"))
        self.assertTrue(generate_sbom._is_commit_hash("abc1234"))
        self.assertFalse(generate_sbom._is_commit_hash("20260526"))
        self.assertFalse(generate_sbom._is_commit_hash("1.2.3"))

    def test_spdx_license_ids_come_from_spdx_kinds_only(self):
        self.assertEqual(
            generate_sbom._spdx_license_id("MIT", "@rules_license+//licenses/spdx:MIT"), "MIT"
        )
        self.assertEqual(
            generate_sbom._spdx_license_id(
                "SPDX-license-identifier-GPL-2.0-only",
                "@@goldfish+//emulator:SPDX-license-identifier-GPL-2.0-only",
            ),
            "GPL-2.0-only",
        )
        self.assertEqual(generate_sbom._spdx_license_id("MIT", "@@qemu+//:custom_mit"), "")
        self.assertEqual(generate_sbom._spdx_license_id("legacy_notice", ""), "")

    def test_fallback_license_text(self):
        noassertion_text = generate_sbom._read_license_text(None, "NOASSERTION")
        self.assertNotIn("NOASSERTION.html", noassertion_text)
        compound_text = generate_sbom._read_license_text(None, "Apache-2.0 AND MIT")
        self.assertNotIn("https://spdx.org/licenses/", compound_text)

    def test_build_spdx_document_structure_and_style_guide(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            crate_dir = os.path.join(tmpdir, "crates", "bitflags")
            lic_file = _write(os.path.join(crate_dir, "LICENSE-APACHE"), "Apache License 2.0")
            _write(
                os.path.join(crate_dir, "METADATA"),
                'name: "bitflags"\n'
                "third_party {\n"
                '  homepage: "https://crates.io/crates/bitflags"\n'
                '  version: "2.13.2"\n'
                "}\n",
            )
            reports = _report(
                [
                    _license(
                        "@@ffmpeg+//:license",
                        package_name="ffmpeg",
                        package_url="https://www.ffmpeg.org",
                        package_version="8.0",
                        copyright_notice="Copyright (C) 2000-2026 FFmpeg Project",
                        license_kinds=[_spdx_kind("GPL-2.0-or-later")],
                    ),
                    _license(
                        "@@goldfish_crates++lrc+bitflags//:license",
                        license_text=lic_file,
                        package_name="",
                        license_kinds=[
                            {"name": "SPDX-license-identifier-Apache-2.0"},
                            {"name": "SPDX-license-identifier-MIT"},
                        ],
                    ),
                ]
            )

            doc = generate_sbom.build_spdx_document(reports, workspace_root=tmpdir)
            self.assertEqual(doc["spdxVersion"], "SPDX-2.3")
            self.assertEqual(doc["dataLicense"], "CC0-1.0")
            self.assertEqual(doc["SPDXID"], "SPDXRef-DOCUMENT")
            self.assertEqual(doc["name"], "sdk-repo-linux-emulator-16500000")
            self.assertIn("Organization: Google LLC", doc["creationInfo"]["creators"])
            self.assertTrue(doc["creationInfo"]["created"])
            self.assertRegex(
                doc["documentNamespace"],
                r"^https://spdx\.google/[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$",
            )

            pkgs = {p["name"]: p for p in doc["packages"]}
            main_pkg = pkgs["sdk-repo-linux-emulator-16500000"]
            self.assertEqual(main_pkg["SPDXID"], "SPDXRef-Package-main")
            self.assertEqual(main_pkg["versionInfo"], "37.1.5-16500000")

            ffmpeg_pkg = pkgs["ffmpeg"]
            self.assertEqual(ffmpeg_pkg["versionInfo"], "8.0")
            self.assertEqual(ffmpeg_pkg["supplier"], "NOASSERTION")
            self.assertTrue(ffmpeg_pkg["licenseConcluded"].startswith("LicenseRef-"))
            self.assertEqual(ffmpeg_pkg["licenseDeclared"], "GPL-2.0-or-later")
            self.assertEqual(ffmpeg_pkg["sourceInfo"], "Bazel license target @@ffmpeg+//:license")

            crate_pkg = pkgs["rust-crate-bitflags"]
            self.assertEqual(crate_pkg["versionInfo"], "2.13.2")
            self.assertEqual(crate_pkg["downloadLocation"], "https://crates.io/crates/bitflags")
            self.assertEqual(crate_pkg["licenseDeclared"], "Apache-2.0 AND MIT")
            self.assertEqual(
                crate_pkg["externalRefs"][0]["referenceLocator"],
                "pkg:cargo/bitflags@2.13.2",
            )

            # Every non-root package concludes to an extracted license with cross references.
            extracted = {lic["licenseId"]: lic for lic in doc["hasExtractedLicensingInfos"]}
            for pkg in doc["packages"][1:]:
                self.assertIn(pkg["licenseConcluded"], extracted)
                self.assertTrue(extracted[pkg["licenseConcluded"]]["seeAlsos"])

            describes = [
                r for r in doc["relationships"] if r["relationshipType"] == "DESCRIBES"
            ]
            self.assertEqual(len(describes), 1)
            self.assertEqual(describes[0]["spdxElementId"], "SPDXRef-DOCUMENT")
            self.assertEqual(describes[0]["relatedSpdxElement"], "SPDXRef-Package-main")

            # The namespace is deterministic and changes with the content.
            again = generate_sbom.build_spdx_document(reports, workspace_root=tmpdir)
            self.assertEqual(again["documentNamespace"], doc["documentNamespace"])
            changed = generate_sbom.build_spdx_document(reports[:0], workspace_root=tmpdir)
            self.assertNotEqual(changed["documentNamespace"], doc["documentNamespace"])

    def test_unknown_license_kinds_are_kept_through_the_license_ref(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            lic = _write(os.path.join(tmpdir, "foo", "LICENSE"), "MIT plus a custom notice")
            licenses = [
                _license(
                    "@@foo+//:license",
                    license_text=lic,
                    package_dir=os.path.join(tmpdir, "foo"),
                    license_kinds=[_spdx_kind("MIT"), {"name": "notice", "label": "@@foo+//:notice"}],
                )
            ]
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                doc = generate_sbom.build_spdx_document(_report(licenses), workspace_root=tmpdir)
            foo = doc["packages"][1]
            self.assertRegex(foo["licenseDeclared"], r"^MIT AND LicenseRef-foo-[0-9a-f]{16}$")
            self.assertEqual(foo["licenseConcluded"], foo["licenseDeclared"].split(" AND ")[1])
            self.assertTrue(_logged(logs, "notice"))

    def test_distinct_license_targets_with_the_same_name_stay_separate(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            metadata = {
                # Multi-line and inline textproto layouts are both supported.
                "crashpad": (
                    "third_party {\n"
                    f'  version: "{"a" * 40}"\n'
                    "  identifier {\n"
                    '    value: "https://example.com/crashpad/lss"\n'
                    "  }\n"
                    "}\n"
                ),
                "breakpad": (
                    f'third_party {{ version: "{"b" * 40}" '
                    'identifier { value: "https://example.com/breakpad/lss" } }\n'
                ),
            }
            for repo, content in metadata.items():
                pkg_dir = os.path.join(tmpdir, repo, "third_party", "lss")
                _write(os.path.join(pkg_dir, "LICENSE"), "Copyright 2005 Google Inc.")
                _write(os.path.join(pkg_dir, "METADATA"), content)
            licenses = [
                _license(
                    f"@@{repo}+//third_party/lss:license",
                    license_text=os.path.join(tmpdir, repo, "third_party", "lss", "LICENSE"),
                    package_dir=os.path.join(tmpdir, repo, "third_party", "lss"),
                )
                for repo in ("crashpad", "breakpad")
            ]
            doc = generate_sbom.build_spdx_document(_report(licenses), workspace_root=tmpdir)

            lss = [p for p in doc["packages"] if p["name"] == "lss"]
            self.assertEqual(len(lss), 2)
            self.assertEqual(len({p["SPDXID"] for p in lss}), 2)
            self.assertEqual({p["versionInfo"] for p in lss}, {"a" * 40, "b" * 40})
            self.assertEqual(
                {p["downloadLocation"] for p in lss},
                {"https://example.com/crashpad/lss", "https://example.com/breakpad/lss"},
            )
            self.assertTrue(all(p["supplier"] == "Organization: Google LLC" for p in lss))

    def test_package_info_is_matched_by_bazel_package(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            licenses, packages = [], []
            for repo, ver in (("crashpad", "1.0"), ("breakpad", "2.0")):
                pkg_dir = os.path.join(tmpdir, repo, "lss")
                licenses.append(
                    _license(
                        f"@@{repo}+//lss:license",
                        license_text=_write(os.path.join(pkg_dir, "LICENSE"), "BSD"),
                        package_dir=pkg_dir,
                    )
                )
                packages.append(
                    {
                        "label": f"@@{repo}+//lss:package_info",
                        "package_name": "lss",
                        "package_version": ver,
                        "package_url": f"https://example.com/{repo}",
                        "purl": "",
                        "package_dir": pkg_dir,
                    }
                )
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                doc = generate_sbom.build_spdx_document(
                    _report(licenses, packages=packages), workspace_root=tmpdir
                )
            self.assertTrue(_logged(logs, "ambiguous"))
            by_url = {p["downloadLocation"]: p["versionInfo"] for p in doc["packages"][1:]}
            self.assertEqual(
                by_url, {"https://example.com/crashpad": "1.0", "https://example.com/breakpad": "2.0"}
            )

    def test_metadata_search_stays_inside_the_license_package(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            repo = os.path.join(tmpdir, "crashpad")
            # Metadata of the enclosing repository must not leak into the nested package.
            _write(
                os.path.join(repo, "METADATA"),
                'third_party {\n  homepage: "https://example.com/crashpad"\n  version: "0.8.0"\n}\n',
            )
            pkg_dir = os.path.join(repo, "third_party", "lss")
            _write(
                os.path.join(pkg_dir, "README.crashpad"),
                "Name: linux-syscall-support\n"
                "URL: https://chromium.googlesource.com/linux-syscall-support/\n"
                "Revision: See DEPS\n",
            )
            lic = _write(os.path.join(pkg_dir, "lss", "LICENSE"), "BSD")
            # Free-form README prose is never used as a download location.
            _write(os.path.join(pkg_dir, "lss", "README.md"), "See https://en.wikipedia.org/wiki/X86-64")

            ver, url = generate_sbom.discover_package_metadata_from_tree(lic, pkg_dir)
            self.assertEqual(ver, "")
            self.assertEqual(url, "https://chromium.googlesource.com/linux-syscall-support/")

            # Without a package directory only the license file's directory is searched.
            self.assertEqual(generate_sbom.discover_package_metadata_from_tree(lic), ("", ""))

            # A license file outside its package only makes the package directory searched.
            other_pkg = os.path.join(repo, "other")
            _write(os.path.join(other_pkg, "VERSION"), "3.4.5\n")
            shared_lic = _write(os.path.join(repo, "LICENSE"), "BSD")
            self.assertEqual(
                generate_sbom.discover_package_metadata_from_tree(shared_lic, other_pkg),
                ("3.4.5", ""),
            )
            # Without a license file the package directory is still searched.
            self.assertEqual(
                generate_sbom.discover_package_metadata_from_tree(None, other_pkg), ("3.4.5", "")
            )
            self.assertEqual(generate_sbom.discover_package_metadata_from_tree(None), ("", ""))

            # A release version anywhere in the package beats a closer Git commit hash.
            _write(
                os.path.join(pkg_dir, "lss", "METADATA"),
                f'third_party {{\n  version: "{"c" * 40}"\n}}\n',
            )
            self.assertEqual(
                generate_sbom.discover_package_metadata_from_tree(lic, pkg_dir)[0], "c" * 40
            )
            _write(os.path.join(pkg_dir, "VERSION"), "\ufeff2.1.0 (2024-01-01)\n")
            self.assertEqual(
                generate_sbom.discover_package_metadata_from_tree(lic, pkg_dir)[0], "2.1.0"
            )

    def test_registry_metadata_layers(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            bazel_dir = os.path.join(tmpdir, "build", "bazel")
            modules = os.path.join(bazel_dir, "registry", "modules")
            _write(
                os.path.join(bazel_dir, "toplevel.MODULE.bazel"),
                "bazel_dep(version = '1.3.2.bcr.1', name = 'zlib')\n"
                'bazel_dep(repo_name = "my_baz", name = "baz", version = "3.0")\n'
                '# bazel_dep(name = "commented", version = "9.9")\n'
                'bazel_dep(name = "rules_testing", version = "0.6.0", dev_dependency = True)\n'
                'include("//build/bazel:vendor.MODULE.bazel")\n',
            )
            _write(
                os.path.join(bazel_dir, "vendor.MODULE.bazel"),
                'bazel_dep(name = "included", version = "5.0")\n',
            )
            for ver in ("1.9.0", "1.10.0"):
                _write(
                    os.path.join(modules, "foo", ver, "source.json"),
                    json.dumps({"url": f"https://example.com/foo-{ver}.tar.gz"}),
                )
            _write(
                os.path.join(modules, "bar", "1.0", "source.json"),
                json.dumps({"url": "https://example.com/bar-1.0.tar.gz"}),
            )
            _write(
                os.path.join(modules, "qux", "0.0.1", "source.json"),
                json.dumps({"type": "local_path", "path": "third_party/qux"}),
            )
            _write(
                os.path.join(modules, "loc", "4.0", "source.json"),
                json.dumps({"type": "local_path", "path": "third_party/loc"}),
            )
            _write(
                os.path.join(modules, "gitmod", "1.0", "source.json"),
                json.dumps(
                    {"type": "git_repository", "remote": "https://github.com/a/b", "commit": "abc123"}
                ),
            )
            registry = "https://bcr.bazel.build/modules"
            _write(
                os.path.join(tmpdir, "MODULE.bazel.lock"),
                json.dumps(
                    {
                        "registryFileHashes": {
                            f"{registry}/bar/2.0/source.json": "sha256-x",
                            f"{registry}/bar/2.0/MODULE.bazel": "sha256-y",
                            f"{registry}/baz/2.5/source.json": "sha256-z",
                            f"{registry}/platforms/0.0.11/source.json": "sha256-p",
                        }
                    }
                ),
            )

            versions, urls = generate_sbom.discover_workspace_registry_metadata(tmpdir)
            self.assertEqual(versions["zlib"], "1.3.2")
            self.assertEqual(versions["included"], "5.0")
            self.assertEqual(versions["foo"], "1.10.0")  # Highest local version, compared numerically.
            self.assertEqual(urls["foo"], "https://example.com/foo-1.10.0.tar.gz")
            # The lockfile (what Bazel selected) beats the local registry and the root module.
            self.assertEqual(versions["bar"], "2.0")
            self.assertEqual(versions["baz"], "2.5")
            # The URL always matches the reported version; bar 2.0 has no local entry.
            self.assertNotIn("bar", urls)
            self.assertEqual(versions["platforms"], "0.0.11")  # Real 0.0.x registry releases.
            self.assertEqual(versions["loc"], "4.0")
            self.assertNotIn("loc", urls)  # local_path modules have no download URL.
            self.assertEqual(urls["gitmod"], "git+https://github.com/a/b@abc123")
            self.assertNotIn("qux", versions)  # 0.0.x local overlays are placeholders.
            self.assertNotIn("commented", versions)
            self.assertNotIn("rules_testing", versions)
            self.assertNotIn("my_baz", versions)

    def test_only_root_packages_use_module_registry_metadata(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            _write(os.path.join(tmpdir, "build", "bazel", "toplevel.MODULE.bazel"), "\n")
            lockfile = {
                "registryFileHashes": {
                    "https://bcr.bazel.build/modules/protobuf/35.1/source.json": "x",
                }
            }
            _write(os.path.join(tmpdir, "MODULE.bazel.lock"), json.dumps(lockfile))
            licenses = []
            for package in ("", "third_party/utf8_range"):
                pkg_dir = os.path.join(tmpdir, "protobuf", package)
                licenses.append(
                    _license(
                        f"@@protobuf+//{package}:license",
                        license_text=_write(os.path.join(pkg_dir, "LICENSE"), "BSD"),
                        package_dir=pkg_dir,
                        package_name="protobuf" if not package else package,
                    )
                )
            doc = generate_sbom.build_spdx_document(_report(licenses), workspace_root=tmpdir)
            versions = {p["name"]: p["versionInfo"] for p in doc["packages"][1:]}
            self.assertEqual(versions, {"protobuf": "35.1", "utf8_range": "unspecified"})

    def test_dynamic_metadata_discovery_from_tree(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            qemu_dir = os.path.join(tmpdir, "third_party", "qemu")
            qemu_lic = _write(os.path.join(qemu_dir, "COPYING"), "GPL-2.0")
            _write(os.path.join(qemu_dir, "_VERSION"), "11.0.2\n")
            ver, _ = generate_sbom.discover_package_metadata_from_tree(qemu_lic, qemu_dir)
            self.assertEqual(ver, "11.0.2")

    def test_crates_without_urls_link_to_the_crates_io_release(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            crate = os.path.join(tmpdir, "remain")
            lic = _write(os.path.join(crate, "LICENSE"), "MIT")
            _write(os.path.join(crate, "Cargo.toml"), '[package]\nname = "remain"\nversion = "0.2.16"\n')
            doc = generate_sbom.build_spdx_document(
                _report(
                    [_license("@@goldfish_crates++lrc+remain//:license", license_text=lic, package_dir=crate, package_name="")]
                ),
                workspace_root=tmpdir,
            )
            self.assertEqual(
                doc["packages"][1]["downloadLocation"], "https://crates.io/crates/remain/0.2.16"
            )

    def test_cargo_toml_reads_only_the_package_table(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            cargo = _write(
                os.path.join(tmpdir, "Cargo.toml"),
                "[package]\n"
                'name = "netsim"\n'
                "version.workspace = true\n"
                "repository = 'https://github.com/google/netsim'\n"
                "\n"
                "[dependencies.serde]\n"
                'version = "1.0.0"\n',
            )
            self.assertEqual(
                generate_sbom._parse_cargo_toml(cargo), ("", "https://github.com/google/netsim")
            )
            _write(cargo, '[package]\nname = "remain"\nversion = "0.2.16"\n')
            self.assertEqual(generate_sbom._parse_cargo_toml(cargo), ("0.2.16", ""))

    def test_meson_version_comes_from_the_project_call(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            meson = _write(
                os.path.join(tmpdir, "meson.build"),
                "# project('fake', version : '0.0.9')\n"
                "project('glib', 'c',\n"
                "  meson_version : '>= 1.2.0',\n"
                "  version : '2.82.2',  # release (2024)\n"
                "  default_options : ['c_std=gnu11'])\n"
                "dep = dependency('libffi', version : '>= 3.0.0')\n",
            )
            self.assertEqual(generate_sbom._parse_meson_build(meson), ("2.82.2", ""))
            _write(meson, "project('x', 'c')\ndep = dependency('y', version : '>= 1.0')\n")
            self.assertEqual(generate_sbom._parse_meson_build(meson), ("", ""))
            _write(meson, "project('x', 'c', version : '1.0'\n")  # Never closed.
            self.assertEqual(generate_sbom._parse_meson_build(meson), ("", ""))

    def test_metadata_textproto_prefers_the_source_location(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            metadata = _write(
                os.path.join(tmpdir, "METADATA"),
                "third_party {\n"
                '  # homepage: "https://example.com/old"\n'
                '  homepage: "https://example.com/home"\n'
                '  identifier { type: "Git" value: "https://example.com/repo.git" version: "v1.2" }\n'
                "}\n",
            )
            self.assertEqual(
                generate_sbom._parse_metadata_file(metadata), ("1.2", "https://example.com/repo.git")
            )

    def test_readme_metadata_uses_structured_fields_only(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            readme = _write(
                os.path.join(tmpdir, "README.version"),
                "URL: http://libpng.download/src/libpng16/libpng-1.6.34.tar.gz\n"
                "Version: 1.6.34\n",
            )
            self.assertEqual(
                generate_sbom._parse_readme_metadata(readme),
                ("1.6.34", "http://libpng.download/src/libpng16/libpng-1.6.34.tar.gz"),
            )
            _write(readme, "Version: master\nRevision: abc1234\n")
            self.assertEqual(generate_sbom._parse_readme_metadata(readme), ("abc1234", ""))
            _write(readme, "This directory contains the header from\nhttps://example.com/x\n")
            self.assertEqual(generate_sbom._parse_readme_metadata(readme), ("", ""))

    def test_purls_are_encoded_and_github_names_lowercased(self):
        self.assertEqual(
            generate_sbom._infer_purl("ffmpeg", "unspecified", "https://www.ffmpeg.org"),
            "pkg:generic/ffmpeg",
        )
        self.assertEqual(
            generate_sbom._infer_purl("libstdc++", "1.0+build", "NOASSERTION"),
            "pkg:generic/libstdc%2B%2B@1.0%2Bbuild",
        )
        self.assertEqual(
            generate_sbom._infer_purl(
                "vulkan-headers", "1.4.354", "https://github.com/KhronosGroup/Vulkan-Headers.git"
            ),
            "pkg:github/khronosgroup/vulkan-headers@1.4.354",
        )
        self.assertEqual(
            generate_sbom._purl_with_version("pkg:generic/foo?arch=x86#sub", "1.2"),
            "pkg:generic/foo@1.2?arch=x86#sub",
        )
        self.assertEqual(
            generate_sbom._purl_with_version("pkg:generic/foo@2.0?a=b", "1.2"),
            "pkg:generic/foo@2.0?a=b",
        )
        self.assertEqual(
            generate_sbom._purl_with_version("pkg:generic/foo", "unspecified"), "pkg:generic/foo"
        )

    def test_supplier_matches_url_hosts_not_substrings(self):
        google = "Organization: Google LLC"
        infer = generate_sbom._infer_supplier
        self.assertEqual(
            infer("@@x+//:license", "https://example.com/?q=android.googlesource.com", "", ""),
            "NOASSERTION",
        )
        self.assertEqual(infer("@@x+//:license", "https://github.com/google/crosvm", "", ""), google)
        self.assertEqual(
            infer("@@aom+//:license", "https://aomedia.googlesource.com/aom", "", ""), "NOASSERTION"
        )
        self.assertEqual(infer("@@x+//:license", "NOASSERTION", "", "Copyright 2020 Google LLC"), google)
        self.assertEqual(infer("@@x+//:license", "NOASSERTION", "Copyright 2015 Google, Inc.", ""), google)

    def test_file_problems_are_logged(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            lic = _write(os.path.join(tmpdir, "LICENSE"), "x" * 64)
            with mock.patch.object(generate_sbom, "_MAX_LICENSE_BYTES", 10):
                with self.assertLogs("generate_sbom", level="WARNING") as logs:
                    self.assertEqual(generate_sbom._read_license_text(lic, "MIT"), "x" * 10)
            self.assertTrue(_logged(logs, "larger than"))
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                text = generate_sbom._read_license_text(os.path.join(tmpdir, "missing"), "MIT")
            self.assertIn("https://spdx.org/licenses/MIT.html", text)
            self.assertTrue(_logged(logs, "not found"))
            latin1 = _write(os.path.join(tmpdir, "COPYING"), "Copyright \xa9 Foo".encode("latin-1"))
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                self.assertIn("Foo", generate_sbom._read_license_text(latin1, "MIT"))
            self.assertTrue(_logged(logs, "not valid UTF-8"))

    def test_lockfile_problems_are_logged(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            lockfile = os.path.join(tmpdir, "MODULE.bazel.lock")
            for content, message in (
                ("[]", "not a JSON object"),
                ('{"lockFileVersion": 18}', "registryFileHashes"),
                ("{not json", "cannot parse"),
            ):
                _write(lockfile, content)
                with self.assertLogs("generate_sbom", level="WARNING") as logs:
                    self.assertEqual(generate_sbom._parse_lockfile(lockfile, warn_if_missing=True), {})
                self.assertTrue(_logged(logs, message), message)
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                generate_sbom._parse_lockfile(os.path.join(tmpdir, "missing.lock"), warn_if_missing=True)
            self.assertTrue(_logged(logs, "not found"))
            # A workspace without a lockfile is reported too.
            _write(os.path.join(tmpdir, "ws", "build", "bazel", "toplevel.MODULE.bazel"), "\n")
            with self.assertLogs("generate_sbom", level="WARNING") as logs:
                generate_sbom.discover_workspace_registry_metadata(os.path.join(tmpdir, "ws"))
            self.assertTrue(_logged(logs, "lockfile not found"))

    def test_missing_workspace_root_is_logged(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            lic = _write(os.path.join(tmpdir, "zlib", "LICENSE"), "zlib")
            with mock.patch.object(generate_sbom, "_find_workspace_root", return_value=None):
                with self.assertLogs("generate_sbom", level="WARNING") as logs:
                    doc = generate_sbom.build_spdx_document(
                        _report([_license("@@zlib+//:license", license_text=lic)])
                    )
            self.assertTrue(_logged(logs, "workspace root"))
            zlib = [p for p in doc["packages"] if p["name"] == "zlib"][0]
            self.assertEqual(zlib["versionInfo"], "unspecified")
            self.assertEqual(zlib["downloadLocation"], "NOASSERTION")

    def test_malformed_report_entries_are_skipped_with_warnings(self):
        with self.assertLogs("generate_sbom", level="WARNING") as logs:
            doc = generate_sbom.build_spdx_document(
                [
                    "not-a-report",
                    {"product_metadata": None, "licenses_list": None, "packages_list": [1]},
                    {"licenses_list": ["x", {"label": 42, "license_kinds": None}]},
                ]
            )
        for message in ("report 0 is not a JSON object", "packages_list", "licenses_list",
                        "has no label", "no version"):
            self.assertTrue(_logged(logs, message), message)
        root = doc["packages"][0]
        self.assertEqual(root["SPDXID"], "SPDXRef-Package-main")
        self.assertEqual(root["versionInfo"], "unspecified")
        self.assertNotIn("@", root["externalRefs"][0]["referenceLocator"])
        self.assertEqual(len(doc["packages"]), 2)
        self.assertEqual(doc["packages"][1]["name"], "unknown")

    def test_cli_main_writes_valid_json(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            in_path = os.path.join(tmpdir, "in.json")
            out_path = os.path.join(tmpdir, "out.spdx.json")
            _write(in_path, json.dumps(_report([], build_id="developer")))
            rc = generate_sbom.main(["--licenses_info", in_path, "--out", out_path])
            self.assertEqual(rc, 0)
            with open(out_path, "r", encoding="utf-8") as f:
                doc = json.load(f)
            self.assertEqual(doc["SPDXID"], "SPDXRef-DOCUMENT")
            self.assertEqual(doc["packages"][0]["versionInfo"], "37.1.5")

    def test_cli_main_rejects_invalid_input(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            out_path = os.path.join(tmpdir, "out.spdx.json")
            in_path = _write(os.path.join(tmpdir, "in.json"), '{"not": "a list"}')
            with self.assertLogs("generate_sbom", level="ERROR"):
                rc = generate_sbom.main(["--licenses_info", in_path, "--out", out_path])
            self.assertEqual(rc, 1)
            with self.assertLogs("generate_sbom", level="ERROR"):
                rc = generate_sbom.main(
                    ["--licenses_info", os.path.join(tmpdir, "missing.json"), "--out", out_path]
                )
            self.assertEqual(rc, 1)
            self.assertFalse(os.path.exists(out_path))


if __name__ == "__main__":
    unittest.main()
