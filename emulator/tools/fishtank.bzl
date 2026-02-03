"""This file defines a Bazel rules to integrate with the prebuilt fishtank zipfile. It includes unzip() to extract the zipfile into a directory and subdir_link() to create a symlink to a subdirectory in the zip directory."""

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

def _subdir_link_impl(ctx):
    subdir = ctx.actions.declare_symlink(ctx.label.name)

    ctx.actions.symlink(
        output = subdir,
        target_path = ctx.file.dir.basename + "/" + ctx.attr.subdir,
        # TODO this needs Bazel 9.0.0 or later: target_type = "directory",
    )

    runfiles = ctx.runfiles([ctx.file.dir])
    return [
        DefaultInfo(
            files = depset(direct = [subdir]),
            runfiles = runfiles,
        ),
    ]

subdir_link = rule(
    _subdir_link_impl,
    attrs = {
        "dir": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "subdir": attr.string(
            mandatory = True,
        ),
    },
)
