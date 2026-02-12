"""Repository rules for local rust crates.

This module provides `patched_new_local_repository`, a rule that copies a local directory
and applies patches to it. This is necessary because the built-in `new_local_repository`
symlinks the directory, making it immutable and unpatchable.
"""

def _patched_new_local_repository_impl(ctx):
    # Calculate workspace root using readlink -f for robustness
    # We resolve the absolute path of the build_file, then traverse up to find the workspace root.
    # The workspace root is the directory containing 'hardware'.

    build_file_path = ctx.path(ctx.attr.build_file)

    # Resolving path using python to be OS agnostic (replaces readlink -f)
    # We rely on python3 being available in path.
    # We normalize to forward slashes to ensure the split below works on Windows.
    res = ctx.execute([
        "python3",
        "-c",
        "import os, sys; print(os.path.realpath(sys.argv[1]).replace(os.sep, '/'), end='')",
        str(build_file_path),
    ])

    if res.return_code != 0:
        fail("Failed to resolve build_file path: " + res.stderr)

    build_file_abs = res.stdout

    # robustly find the workspace root by splitting on the known goldfish path
    # build_file_abs should contain "hardware/generic/goldfish"

    if "hardware/generic/goldfish" in build_file_abs:
        workspace_root = build_file_abs.split("/hardware/generic/goldfish")[0]
    else:
        # Fallback for unexpected layout
        fail("Could not determine workspace root from build file path: " + build_file_abs)

    src_path = workspace_root + "/" + ctx.attr.path

    # Copy source to root using python (replaces cp -fR)
    # dirs_exist_ok=True requires Python 3.8+
    res = ctx.execute([
        "python3",
        "-c",
        "import shutil, sys, os; shutil.copytree(sys.argv[1], sys.argv[2], dirs_exist_ok=True)",
        str(src_path),
        ".",
    ])

    if res.return_code != 0:
        fail("Failed to copy source: " + res.stderr)

    # Symlink build file
    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

    # Apply patches
    for patch in ctx.attr.patches:
        ctx.patch(patch, strip = 1)

patched_new_local_repository = repository_rule(
    implementation = _patched_new_local_repository_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "build_file": attr.label(mandatory = True),
        "patches": attr.label_list(default = []),
    },
)
