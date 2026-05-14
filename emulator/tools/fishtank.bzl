"""Bazel rules to integrate with the prebuilt fishtank zipfile. It includes unzip() to extract the zipfile into a directory and subdir_link() to create a symlink to a subdirectory in the zip directory."""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")

def _unzip_impl(ctx):
    root = ctx.actions.declare_directory(ctx.attr.name)
    args = ctx.actions.args()
    args.add("x")
    args.add(ctx.file.zipfile.path)
    args.add_all(["-d", root.path])

    ctx.actions.run(
        outputs = [root],
        inputs = [ctx.file.zipfile],
        arguments = [args],
        mnemonic = "Extracting",
        executable = ctx.executable._zip,
    )

    return [
        DefaultInfo(
            files = depset(direct = [root]),
        ),
    ]

unzip = rule(
    _unzip_impl,
    attrs = {
        "zipfile": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "_zip": attr.label(
            allow_single_file = True,
            default = "@bazel_tools//tools/zip:zipper",
            cfg = "exec",
            executable = True,
        ),
    },
)

def _subpath_link_impl(ctx):
    # TODO(whollins): check path separators in subpath.
    link = ctx.actions.declare_symlink(ctx.label.name)

    fishtank_dir = ctx.attr.fishtank_dir[BuildSettingInfo].value if ctx.attr.fishtank_dir else ""

    # If an override directory is provided via the build setting, symlink directly to it.
    # This completely decouples the target from parent_dir, pruning the unzip action from execution.
    if fishtank_dir:
        ctx.actions.symlink(
            output = link,
            target_path = fishtank_dir,
            target_type = "directory" if ctx.attr.subpath_is_dir else "file",
        )
        return [
            DefaultInfo(
                files = depset(direct = [link]),
            ),
        ]

    ctx.actions.symlink(
        output = link,
        target_path = ctx.file.parent_dir.basename + "/" + ctx.attr.subpath,
        target_type = "directory" if ctx.attr.subpath_is_dir else "file",
    )

    runfiles = ctx.runfiles([ctx.file.parent_dir])
    return [
        DefaultInfo(
            files = depset(direct = [link]),
            runfiles = runfiles,
        ),
    ]

subpath_link = rule(
    _subpath_link_impl,
    doc = """Creates a symlink to a subdirectory of a parent directory or an override path.

    When `fishtank_dir` is provided via build setting, it links directly to the override path
    and skips evaluating `parent_dir`.
    """,
    attrs = {
        "parent_dir": attr.label(
            allow_single_file = True,
            mandatory = True,
            doc = "The unzipped parent directory containing the target subpath.",
        ),
        "subpath": attr.string(
            mandatory = True,
            doc = "Relative subpath inside parent_dir to link to.",
        ),
        "subpath_is_dir": attr.bool(
            default = False,
            doc = "Whether the target subpath is a directory.",
        ),
        "fishtank_dir": attr.label(
            providers = [BuildSettingInfo],
            default = None,
            doc = "Optional build setting providing an absolute override path to link directly.",
        ),
    },
)
