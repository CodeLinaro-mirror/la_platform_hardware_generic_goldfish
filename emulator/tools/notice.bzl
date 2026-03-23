"""Generate Notice MD File"""

load(
    "@rules_license//rules:gather_licenses_info.bzl",
    "gather_licenses_info",
)
load(
    "@rules_license//rules:providers.bzl",
    "TransitiveLicensesInfo",
)

def _generate_notice_file_impl(ctx):
    # Note: Modified from licenses_info_to_json found in rules_license/rules/generate_notice.bzl
    # Declare the intermediate JSON file
    licenses_file = ctx.actions.declare_file("_%s_licenses_info.json" % ctx.label.name)
    licenses_json = []
    license_files = []

    # We only care about a subset of fields from TransitiveLicensesInfo
    for dep in ctx.attr.deps:
        if TransitiveLicensesInfo in dep:
            for license in dep[TransitiveLicensesInfo].licenses.to_list():
                kinds = [{"name": kind.name} for kind in license.license_kinds]

                text_path = None
                if license.license_text:
                    text_path = license.license_text.path
                    license_files.append(license.license_text)

                licenses_json.append({
                    "label": str(license.label),
                    "package_name": license.package_name,
                    "package_url": license.package_url,
                    "package_version": getattr(license, "package_version", None),
                    "copyright_notice": getattr(license, "copyright_notice", None),
                    "license_kinds": kinds,
                    "license_text": text_path,
                })

    ctx.actions.write(
        output = licenses_file,
        content = json.encode([{"licenses_list": licenses_json}]),
    )

    inputs = [licenses_file] + license_files
    outputs = [ctx.outputs.out]

    args = ctx.actions.args()
    args.add("--licenses_info", licenses_file.path)
    args.add("--out", ctx.outputs.out.path)

    # json --> markdown.
    ctx.actions.run(
        mnemonic = "GenerateNotice",
        progress_message = "Generating license notice for %s" % ctx.label,
        inputs = inputs,
        outputs = outputs,
        executable = ctx.executable._generator,
        arguments = [args],
    )

    return [
        DefaultInfo(files = depset(outputs)),
        OutputGroupInfo(licenses_file = depset([licenses_file])),
    ]

_generate_notice_file = rule(
    implementation = _generate_notice_file_impl,
    attrs = {
        "deps": attr.label_list(
            aspects = [gather_licenses_info],
        ),
        "out": attr.output(mandatory = True),
        "_generator": attr.label(
            default = Label("//emulator/tools:generate_notice"),
            executable = True,
            cfg = "exec",
        ),
    },
)

def generate_notice_file(**kwargs):
    _generate_notice_file(**kwargs)
