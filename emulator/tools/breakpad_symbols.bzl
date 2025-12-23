"""Bazel rule for extracting breakpad symbols."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load(
    "@goldfish_build//rules/native:debug.bzl",
    "DebugSymbolsSetInfo",
    "FissionPackageSetInfo",
    "collect_fission_package_aspect",
    "collect_pdb_aspect",
    "gen_dsym_aspect",
)

visibility("//emulator/...")

def windows_path(p):
    # type: (string) -> string
    return p.replace("/", "\\")

def _maybe_executable(file):
    # type: (File) -> bool
    if not file.extension:  # Assume files with no extension are executables.
        return True
    if file.extension in ["so", "dylib", "exe", "dll"]:
        return True
    stem, extension = paths.split_extension(file.basename)
    if extension[1:].isdigit() and stem.endswith(".so"):
        # liba.so.1
        return True
    return False

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
    split_symbol_lookup = {}
    for binary in ctx.attr.binaries:
        if DebugSymbolsSetInfo in binary:
            symbol_set = binary[DebugSymbolsSetInfo]
            if symbol_set.dsym:
                for symbol in symbol_set.dsym.to_list():
                    split_symbol_lookup[symbol.executable_file] = ("dsym", symbol.original_executable_file, symbol.dsym_bundle)
            if symbol_set.pdb:
                for symbol in symbol_set.pdb.to_list():
                    split_symbol_lookup[symbol.executable_file] = ("pdb", symbol.original_executable_file, symbol.pdb_file)
        if FissionPackageSetInfo in binary:
            symbol_set = binary[FissionPackageSetInfo]
            if symbol_set.fission_package:
                for symbol in symbol_set.fission_package.to_list():
                    split_symbol_lookup[symbol.executable_file] = ("dwp", symbol.original_executable_file, symbol.dwp_file)

    output_files = []

    for candidate in ctx.files.binaries:
        if candidate not in split_symbol_lookup and not _maybe_executable(candidate):
            continue

        input_file = candidate
        input_files = [input_file]
        prepend_args = []
        append_args = []
        if input_file in split_symbol_lookup:
            symbol_type, original_executable, split_symbol = split_symbol_lookup[input_file]
            input_file = original_executable or input_file
            input_files = [input_file, split_symbol]
            if symbol_type == "dsym":
                prepend_args.extend(["-g", split_symbol.path])
            elif symbol_type == "dwp":
                append_args.append(split_symbol.dirname)
            elif symbol_type == "pdb":
                input_file = split_symbol
                prepend_args.append("--i")  # Generate INLINE/INLINE_ORIGIN records, only valid when reading PDBs
        elif input_file.extension in ["exe", "dll"]:
            prepend_args.append("--pe")  # No PDB available - read from PE instead.

        output_name = str(
            hash(candidate.dirname),
        ) + "/" + paths.replace_extension(candidate.basename, ".sym")
        output_file = ctx.actions.declare_file(output_name)
        output_files.append(output_file)

        if ctx.target_platform_has_constraint(
            ctx.attr._target_windows[platform_common.ConstraintValueInfo],
        ):
            ctx.actions.run(
                mnemonic = "ExtractBreakpadSymbols",
                outputs = [output_file],
                inputs = input_files,
                executable = ctx.executable._dump_syms,
                arguments = prepend_args + [
                    "--f",  # Output to:
                    windows_path(output_file.path),
                    windows_path(input_file.path),
                ] + append_args,
            )
        else:
            ctx.actions.run(
                mnemonic = "ExtractBreakpadSymbols",
                outputs = [output_file],
                inputs = input_files,
                executable = ctx.executable._dump_syms,
                arguments = prepend_args + [
                    "-d",  # Generate INLINE/INLINE_ORIGIN records
                    "-m",  # Handle multiple symbols at same address, if any.
                    "-f",  # Output to:
                    output_file.path,
                    input_file.path,
                ] + append_args,
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
            aspects = [
                gen_dsym_aspect,
                collect_fission_package_aspect,
                collect_pdb_aspect,
            ],
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
