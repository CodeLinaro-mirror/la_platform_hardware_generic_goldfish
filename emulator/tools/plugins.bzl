"""This file defines a Bazel rule `collect_plugins` that creates symbolic links to plugin binaries from a specified output directory, allowing the launcher to easily access them."""

def _collect_plugins_impl(ctx):
    """Collects the location of all plugins and creates symlinks to them from an output directory.

    This function takes a list of plugin targets as input and creates symbolic
    links to their output files in the specified output directory. This allows
    the launcher to easily access the plugins without having to know their exact
    locations.

    For example:

    The plugins:
        "//third_party/qemu:hw-display-virtio-vga",
        "//hardware/generic/goldfish/emulator/plugin/sample",

    And output directory "plugins" will create the links:

    plugins/hw-display-virtio-vga-> //third_party/qemu:hw-display-virtio-vga
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
        "//third_party/qemu:hw-display-virtio-vga",
        "//hardware/generic/goldfish/emulator/plugin/sample",

    And output directory "plugins" will create the links:

    plugins/hw-display-virtio-vga-> //third_party/qemu:hw-display-virtio-vga
    plugins/sample -> //hardware/generic/goldfish/emulator/plugin/sample
    """,
)
