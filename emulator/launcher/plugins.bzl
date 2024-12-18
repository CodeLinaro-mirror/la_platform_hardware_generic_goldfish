"""This file defines a Bazel rule `collect_plugins` that creates symbolic links to plugin binaries from a specified output directory, allowing the launcher to easily access them."""

load("@rules_pkg//pkg:providers.bzl", "PackageVariablesInfo")

def _collect_plugins_impl(ctx):
    """Collects the location of all plugins and creates symlinks to them from an output directory.

    This function takes a list of plugin targets as input and creates symbolic
    links to their output files in the specified output directory. This allows
    the launcher to easily access the plugins without having to know their exact
    locations.

    For example:

    The plugins:
        "//external/qemu:hw-display-virtio-vga",
        "//hardware/generic/goldfish/emulator/plugin/sample",

    And output directory "plugins" will create the links:

    plugins/hw-display-virtio-vga-> //external/qemu:hw-display-virtio-vga
    plugins/sample -> //hardware/generic/goldfish/emulator/plugin/sample

    Args:
        ctx: The Bazel context object.

    Returns:
        A DefaultInfo object containing the list of symbolic links created.
    """
    deps = []
    for dep in ctx.attr.plugins:
        output_files = dep.files.to_list()
        for output in output_files:
            # Create a symbolic link for each output file in the plugins directory
            link = ctx.actions.declare_file(
                ctx.attr.output_dir + "/" + output.basename,
            )
            deps.append(link)
            ctx.actions.symlink(
                output = link,
                target_file = output,
            )

    return DefaultInfo(files = depset(deps))

collect_plugins = rule(
    implementation = _collect_plugins_impl,
    attrs = {
        "plugins": attr.label_list(
            allow_files = True,
            providers = ["files"],
            doc = "A list of plugin targets to collect. Each target should provide the 'files' provider.",
        ),
        "output_dir": attr.string(
            mandatory = True,
            doc = "The directory where the symbolic links to the plugins will be created.",
        ),
    },
    doc = """
    Collects the location of all plugins and creates symlinks to them from an output directory.

    This rule takes a list of plugin targets as input and creates symbolic
    links to their output files in the specified output directory. This allows
    the launcher to easily access the plugins without having to know their exact
    locations.

    For example:

    The plugins:
        "//external/qemu:hw-display-virtio-vga",
        "//hardware/generic/goldfish/emulator/plugin/sample",

    And output directory "plugins" will create the links:

    plugins/hw-display-virtio-vga-> //external/qemu:hw-display-virtio-vga
    plugins/sample -> //hardware/generic/goldfish/emulator/plugin/sample
    """,
)

def _expand_impl(ctx):
    # values = {}

    # # Copy attributes from the rule to the provider
    # values["product_name"] = ctx.attr.product_name
    # values["version"] = ctx.attr.version
    # values["revision"] = ctx.attr.revision
    # values["platform"] = ctx.attr.platform

    # # Add some well known variables from the rule context.
    # values["target_cpu"] = ctx.var.get("TARGET_CPU")
    # values["compilation_mode"] = ctx.var.get("COMPILATION_MODE")

    # build_id_dep = ctx.attr.build_id_dep[PackageVariablesInfo]
    # values["build_id"] = build_id_dep.values["build_id"]

    version_info = ctx.attr.version_info[PackageVariablesInfo]
    ctx.actions.expand_template(
        template = ctx.file.template,
        output = ctx.outputs.source_file,
        substitutions = {
            "@BUILD_ID": version_info.values["build_id"],
        },
    )

expand = rule(
    implementation = _expand_impl,
    attrs = {
        "version_info": attr.label(
            providers = [PackageVariablesInfo],
        ),
        "template": attr.label(
            default = Label(":version_template.h.in"),
            allow_single_file = True,
        ),
    },
    outputs = {"source_file": "version.h"},
)
