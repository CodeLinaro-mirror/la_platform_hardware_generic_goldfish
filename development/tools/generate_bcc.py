#!/usr/bin/env python3
"""
Automated Compilation Database (compile_commands.json) generator for the Goldfish project.

This script leverages Bazel aspects to automatically discover all C++ targets
within the workspace (@goldfish and related repositories like @aemu, @gfxstream,
and @qemu). It triggers snippet generation without full compilation, aggregates
the results, and installs the final compilation database to the workspace root.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path


def find_workspace_root():
    """
    Finds the absolute path to the Bazel workspace root.

    Searches upwards from the current working directory for a WORKSPACE or
    MODULE.bazel file.

    Returns:
        Path: The absolute path to the workspace root, or None if not found.
    """
    curr = Path(os.getcwd()).absolute()
    for parent in [curr] + list(curr.parents):
        if (parent / "WORKSPACE").exists() or (parent / "MODULE.bazel").exists():
            return parent
    return None


def main():
    """
    Orchestrates the generation, merging, and installation of the compilation database.

    1. Parses command-line arguments to determine the workspace root.
    2. Runs 'bazel build' with the compile_commands_aspect on all goldfish targets.
    3. Collects all generated JSON snippets from the bazel-bin directory.
    4. Merges individual snippets into a single database using 'bazel run'.
    5. Installs the combined database to the workspace root using 'bazel run'.
    """
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json for the Goldfish project."
    )
    parser.add_argument(
        "--workspace",
        type=Path,
        help="Path to the Bazel workspace root. Defaults to automatic discovery.",
    )
    args = parser.parse_args()

    if args.workspace:
        workspace_root = args.workspace.absolute()
        if (
            not (workspace_root / "WORKSPACE").exists()
            and not (workspace_root / "MODULE.bazel").exists()
        ):
            print(
                f"Error: {workspace_root} does not appear to be a Bazel workspace root.",
                file=sys.stderr,
            )
            sys.exit(1)
    else:
        workspace_root = find_workspace_root()

    if not workspace_root:
        print(
            "Error: Could not find workspace root. Please specify it with --workspace.",
            file=sys.stderr,
        )
        sys.exit(1)

    os.chdir(workspace_root)

    print(f"Working in workspace: {workspace_root}")
    print("Building compilation database snippets...")
    # We build everything in @goldfish//... with the aspect.
    # This automatically discovers all C++ targets.
    try:
        subprocess.run(
            [
                "bazel",
                "build",
                "@goldfish//...",
                "--aspects=@goldfish_build//rules:compile_commands.bzl%compile_commands_aspect",
                "--output_groups=report",
                "--nocheck_visibility",
            ],
            check=True,
        )
    except subprocess.CalledProcessError:
        print(
            "Warning: Bazel build failed, some snippets might be missing.",
            file=sys.stderr,
        )

    # Find all generated JSON snippets.
    # Note: Bazel may clear the 'bazel-bin' convenience symlink when targets transition
    # to multiple configurations. Therefore, we search both 'bazel-bin' and all configuration
    # subdirectories in 'bazel-out' (e.g. bazel-out/*/bin).
    # We focus on the ones from @goldfish, @aemu, @gfxstream, and @qemu
    allowed_repos = ["goldfish+", "aemu+", "gfxstream+", "qemu+"]

    search_dirs = []
    bazel_bin = workspace_root / "bazel-bin"
    if bazel_bin.exists():
        search_dirs.append(bazel_bin)

    bazel_out = workspace_root / "bazel-out"
    if bazel_out.exists():
        for config_dir in bazel_out.iterdir():
            if config_dir.is_dir() and not config_dir.name.startswith("."):
                bin_dir = config_dir / "bin"
                if bin_dir.exists():
                    search_dirs.append(bin_dir)

    snippets = set()

    for bin_dir in search_dirs:
        # Check the main workspace (if any)
        for p in bin_dir.glob("bazel_compile_commands_*.json"):
            snippets.add(p.resolve())

        # Check external repositories
        external_dir = bin_dir / "external"
        if external_dir.exists():
            for repo in allowed_repos:
                repo_dir = external_dir / repo
                if repo_dir.exists():
                    for p in repo_dir.rglob("bazel_compile_commands_*.json"):
                        snippets.add(p.resolve())

    if not snippets:
        print(
            "Error: No compilation snippets found in bazel-bin or bazel-out.",
            file=sys.stderr,
        )
        sys.exit(1)

    # Write the list of snippets to a temporary file for the combiner
    inputs_file = workspace_root / "bazel_compile_commands_inputs.txt"
    final_json = workspace_root / "bazel_compile_commands_combined.json"

    with open(inputs_file, "w") as f:
        for s in sorted(snippets):
            f.write(str(s) + "\n")

    try:
        # Use the existing combiner tool via 'bazel run' to create the final compile_commands.json.
        # This ensures the tool is built and its dependencies are resolved.
        subprocess.run(
            [
                "bazel",
                "run",
                "@goldfish_build//utils:gen-cc-snippet",
                "--",
                "combine",
                "-o",
                str(final_json),
                "--inputs_file",
                str(inputs_file),
            ],
            check=True,
        )

        # Finally, install it to the workspace root.
        # Bazel automatically sets BUILD_WORKSPACE_DIRECTORY when running via 'bazel run'.
        subprocess.run(
            [
                "bazel",
                "run",
                "@goldfish_build//utils:gen-cc-snippet",
                "--",
                "install",
                "--input_file",
                str(final_json),
            ],
            check=True,
        )
        print("\nSuccess: compile_commands.json generated and installed.")
    finally:
        # Cleanup temporary files
        inputs_file.unlink(missing_ok=True)
        final_json.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
