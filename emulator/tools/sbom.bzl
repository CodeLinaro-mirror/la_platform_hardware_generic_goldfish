"""Generate SPDX 2.3 JSON SBOM File for Android Emulator."""

load(
    "@rules_license//rules:gather_licenses_info.bzl",
    "gather_licenses_info",
)
load(
    "@rules_license//rules_gathering:gather_metadata.bzl",
    "gather_metadata_info",
)
load(
    "@rules_license//rules_gathering:gathering_providers.bzl",
    "TransitiveLicensesInfo",
    "TransitiveMetadataInfo",
)
load(
    "@rules_pkg//pkg:providers.bzl",
    "PackageVariablesInfo",
)
load(
    "//emulator/tools:packaging.bzl",
    "substitute_package_variables",
)

def _package_dir(label):
    """Returns the execution-root-relative directory of the Bazel package of `label`, or None."""
    if type(label) != "Label":
        return None
    return "/".join([part for part in (label.workspace_root, label.package) if part])

def _extract_licenses_and_packages(deps):
    """Collects LicenseInfo and PackageInfo dicts and license text files from deps."""
    licenses_json = []
    packages_json = []
    license_files = []
    seen_labels = {}
    seen_pkg_labels = {}

    for dep in deps:
        if TransitiveLicensesInfo in dep:
            lic_depset = getattr(dep[TransitiveLicensesInfo], "licenses", None)
            for license in (lic_depset.to_list() if lic_depset else []):
                raw_lic_label = getattr(license, "label", None)
                label_str = str(raw_lic_label) if raw_lic_label != None else ""
                if label_str:
                    if label_str in seen_labels:
                        continue
                    seen_labels[label_str] = True

                raw_kinds = getattr(license, "license_kinds", None) or []
                kinds = [
                    {
                        "name": getattr(kind, "name", None) or "",
                        "label": str(getattr(kind, "label", None) or ""),
                        "conditions": getattr(kind, "conditions", None) or [],
                    }
                    for kind in raw_kinds
                    if kind != None
                ]
                text_path = None
                lic_text = getattr(license, "license_text", None)
                if lic_text and type(lic_text) == "File":
                    text_path = lic_text.path
                    license_files.append(lic_text)

                licenses_json.append({
                    "label": label_str,
                    "package_name": getattr(license, "package_name", None) or "",
                    "package_url": getattr(license, "package_url", None) or "",
                    "package_version": getattr(license, "package_version", None) or "",
                    "copyright_notice": getattr(license, "copyright_notice", None) or "",
                    "license_kinds": kinds,
                    "license_text": text_path,
                    # The generator only reads metadata files inside this package directory.
                    "package_dir": _package_dir(raw_lic_label),
                })

        if TransitiveMetadataInfo in dep:
            other_meta = getattr(dep[TransitiveMetadataInfo], "other_metadata", None)
            for mi in (other_meta.to_list() if other_meta else []):
                raw_label = getattr(mi, "label", None)
                mi_label = str(raw_label) if raw_label != None else ""
                if mi_label:
                    if mi_label in seen_pkg_labels:
                        continue
                    seen_pkg_labels[mi_label] = True
                mi_type = getattr(mi, "type", None) or ""
                if mi_type == "package_info":
                    data = {
                        "package_name": getattr(mi, "package_name", None),
                        "package_url": getattr(mi, "package_url", None),
                        "package_version": getattr(mi, "package_version", None),
                        "purl": getattr(mi, "purl", None),
                    }
                elif mi_type == "package_info_alt":
                    data = getattr(mi, "data", None)
                    if type(data) != "dict":
                        data = {}
                else:
                    continue
                packages_json.append({
                    "label": mi_label,
                    "package_name": data.get("package_name") or "",
                    "package_url": data.get("package_url") or "",
                    "package_version": data.get("package_version") or "",
                    "purl": data.get("purl") or "",
                    "package_dir": _package_dir(raw_label),
                })

    return licenses_json, packages_json, license_files

def _generate_sbom_file_impl(ctx):
    licenses_file = ctx.actions.declare_file("_%s_sbom_licenses_info.json" % ctx.label.name)
    licenses_json, packages_json, license_files = _extract_licenses_and_packages(ctx.attr.deps)

    version_values = {}
    if ctx.attr.package_variables:
        version_values = dict(ctx.attr.package_variables[PackageVariablesInfo].values)

    report_payload = [{
        "top_level_target": str(ctx.label),
        "product_metadata": version_values,
        "licenses_list": licenses_json,
        "packages_list": packages_json,
    }]

    ctx.actions.write(
        output = licenses_file,
        content = json.encode(report_payload),
    )

    default_out = ctx.outputs.out
    actual_out = default_out
    outputs = [default_out]

    # If package_file_name is set (e.g. for DIST_DIR artifact naming), write directly
    # to the variable-substituted file and symlink default_out to it (matching packaging.bzl)
    if ctx.attr.package_file_name:
        output_name = substitute_package_variables(ctx, ctx.attr.package_file_name)
        if output_name != default_out.basename:
            actual_out = ctx.actions.declare_file(output_name)
            ctx.actions.symlink(output = default_out, target_file = actual_out)
            outputs = [actual_out]

    input_files = [licenses_file] + license_files + ctx.files.metadata_srcs
    args = ctx.actions.args()
    args.add("--licenses_info", licenses_file)
    args.add("--out", actual_out)
    if ctx.file.module_lockfile:
        input_files.append(ctx.file.module_lockfile)
        args.add("--module_lockfile", ctx.file.module_lockfile)
    args.add_all(ctx.files.metadata_srcs, before_each = "--metadata_src")

    ctx.actions.run(
        mnemonic = "GenerateSBOM",
        progress_message = "Generating SPDX 2.3 SBOM for %{label}",
        inputs = depset(input_files),
        outputs = [actual_out],
        executable = ctx.executable._generator,
        arguments = [args],
        # The generator also reads upstream metadata files (METADATA, Cargo.toml, VERSION,
        # README.*, ...) inside each license's package and the workspace Bzlmod registry.
        # They cannot be enumerated at analysis time, so they are not declared inputs. The
        # action therefore runs outside the sandbox to see the real source tree, and its
        # result is kept out of the disk and remote caches so that clean builds (such as CI
        # release builds) always regenerate the SBOM from the current tree. Incremental
        # builds re-run the action only when a declared input (a license() target, a license
        # file, or the generator) changes; run `bazel clean` to pick up other metadata edits.
        execution_requirements = {
            "no-cache": "1",
            "no-remote": "1",
            "no-sandbox": "1",
        },
    )

    return [
        DefaultInfo(files = depset(outputs)),
        OutputGroupInfo(
            licenses_file = depset([licenses_file]),
            sbom = depset(outputs),
        ),
    ]

_generate_sbom_file = rule(
    implementation = _generate_sbom_file_impl,
    doc = "Generates an SPDX 2.3 JSON SBOM for the license() targets reachable from `deps`.",
    attrs = {
        "deps": attr.label_list(
            aspects = [gather_licenses_info, gather_metadata_info],
            doc = "Targets included in the release whose transitive dependencies form the SBOM.",
        ),
        "metadata_srcs": attr.label_list(
            allow_files = True,
            # A workspace registry file exported by @goldfish_build; the generator walks up from
            # it to find the workspace root and its Bzlmod registry.
            default = [Label("@goldfish_build//:registry/modules/goldfish/0.0.1/MODULE.bazel")],
            doc = "Workspace files used to locate the workspace root and its Bzlmod registry.",
        ),
        "module_lockfile": attr.label(
            allow_single_file = True,
            doc = "Optional MODULE.bazel.lock to use instead of the workspace lockfile.",
        ),
        "out": attr.output(
            mandatory = True,
            doc = "Output SPDX JSON file.",
        ),
        "package_file_name": attr.string(
            default = "",
            doc = "Optional variable-substituted output filename for release distribution (e.g. sdk-repo-{platform}-{product_name}-sbom-{build_id}.spdx.json).",
        ),
        "package_variables": attr.label(
            providers = [PackageVariablesInfo],
            doc = "PackageVariablesInfo provider containing emulator version/platform/build_id metadata.",
        ),
        "_generator": attr.label(
            default = Label("//emulator/tools:generate_sbom"),
            executable = True,
            cfg = "exec",
        ),
    },
)

def generate_sbom_file(name, **kwargs):
    _generate_sbom_file(name = name, **kwargs)
