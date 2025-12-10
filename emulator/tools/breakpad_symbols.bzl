"""Bazel rule for extracting breakpad symbols."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@goldfish_build//rules/native:dsym.bzl", "AppleDsymInfo", "gen_dsym_aspect")
load("@rules_cc//cc/common:debug_package_info.bzl", "DebugPackageInfo")

visibility("//emulator/...")

def windows_path(p):
    # type: (string) -> string
    return p.replace("/", "\\")

def _breakpad_symbols_impl(ctx):
    """Extracts symbols from binaries using dump_syms.

    This function iterates over a list of binaries, uses `dump_syms` to
    generate a `.sym` file for each binary, and returns a `DefaultInfo` provider
    containing the generated symbol files.

    Args:
        ctx: The rule context.

    Returns:
        A `DefaultInfo` provider containing the generated symbol files.
    """
    output_files = []

    # Iterate over binaries and generate `.sym` files
    for binary_target in ctx.attr.binaries:
        owner_label = binary_target.label
        split_symbol_args = []  # type: list[string]
        split_symbol_files = []  # type: list[File]
        if AppleDsymInfo in binary_target:
            split_symbol_args.extend(["-g", binary_target[AppleDsymInfo].dsym_bundle.path])
            split_symbol_files.append(binary_target[AppleDsymInfo].dsym_bundle)
            binary_files = [binary_target[AppleDsymInfo].executable_file]  # type: list[File]
        elif OutputGroupInfo in binary_target and hasattr(binary_target[OutputGroupInfo], "pdb_file"):
            split_symbol_files = binary_target[OutputGroupInfo].pdb_file.to_list()  # type: list[File]
            binary_files = [binary_target.files_to_run.executable or binary_target.files.to_list()[0]]
        elif DebugPackageInfo in binary_target and binary_target[DebugPackageInfo].dwp_file:
            split_symbol_files.append(binary_target[DebugPackageInfo].dwp_file)
            binary_files = [binary_target[DebugPackageInfo].unstripped_file]
        elif ctx.target_platform_has_constraint(
            ctx.attr._target_windows[platform_common.ConstraintValueInfo],
        ):
            # On Windows with --config=release, for Rutabaga, this produces .../rutabaga_ffi.dll and
            # .../rutabaga_ffi.dll.lib. We can't get symbols from .lib so we just take the first element.
            #binary_files = [binary_target.files.to_list()[0]]  # type: list[File]
            # Skipping Rust binaries for now as there seem to be other issues on buildbots.
            # TODO(b/421925658): Re-enable this.
            binary_files = []
        else:
            binary_files = binary_target.files.to_list()  # type: list[File]
        for binary in binary_files:
            output_name = "/".join([
                owner_label.package,
                paths.replace_extension(binary.basename, ".sym"),
            ])
            if owner_label.repo_name:
                output_name = "_" + owner_label.repo_name + "/" + output_name
            output_file = ctx.actions.declare_file(output_name)
            output_files.append(output_file)

            if ctx.target_platform_has_constraint(
                ctx.attr._target_windows[platform_common.ConstraintValueInfo],
            ):
                ctx.actions.run(
                    mnemonic = "ExtractBreakpadSymbols",
                    outputs = [output_file],
                    inputs = [binary] + split_symbol_files,
                    executable = ctx.executable._dump_syms,
                    arguments = [
                        "--i",  # Generate INLINE/INLINE_ORIGIN records
                        "--f",  # Output to:
                        windows_path(output_file.path),
                        windows_path(binary.path),
                    ],
                )
            else:
                ctx.actions.run(
                    mnemonic = "ExtractBreakpadSymbols",
                    outputs = [output_file],
                    inputs = [binary] + split_symbol_files,  # Simplified: directly use binary
                    executable = ctx.executable._dump_syms,
                    arguments = split_symbol_args + [
                        "-d",  # Generate INLINE/INLINE_ORIGIN records
                        "-m",  # Handle multiple symbols at same address, if any.
                        "-f",  # Output to:
                        output_file.path,
                        binary.path,
                    ],
                )

    return DefaultInfo(files = depset(output_files))

# Define the rule
breakpad_symbols = rule(
    implementation = _breakpad_symbols_impl,
    attrs = {
        "binaries": attr.label_list(
            allow_files = True,
            mandatory = True,
            doc = "The list of binaries to extract symbols from.",
            aspects = [gen_dsym_aspect],
        ),
        "_dump_syms": attr.label(
            default = Label("@breakpad//:dump_syms"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
            doc = "The dump_syms executable. Defaults to @breakpad//:dump_syms.",
        ),
        "_target_windows": attr.label(default = "@platforms//os:windows"),
    },
)
