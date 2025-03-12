"""Bazel rules for extracting and packaging debug symbols."""

load("@//build/bazel/toolchains/cc/mac_clang:dsym.bzl", "AppleDsymInfo", "gen_dsym_aspect")
load("@bazel_skylib//lib:paths.bzl", "paths")
load("@rules_cc//cc/common:debug_package_info.bzl", "DebugPackageInfo")
load("@rules_pkg//pkg:providers.bzl", "PackageVariablesInfo")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

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
            binary_files = binary_target[OutputGroupInfo].pdb_file.to_list()  # type: list[File]
        elif DebugPackageInfo in binary_target and binary_target[DebugPackageInfo].dwp_file:
            split_symbol_files.append(binary_target[DebugPackageInfo].dwp_file)
            binary_files = [binary_target[DebugPackageInfo].unstripped_file]
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
                    inputs = [binary],
                    # dump_syms writes to stdout on Windows - capture and redirect to a file with cmd.
                    executable = "cmd.exe",
                    tools = [ctx.executable._dump_syms],
                    arguments = [
                        "/Q",  # Quiet
                        "/D",  # No autorun commands
                        "/C",  # Run command
                        windows_path(ctx.executable._dump_syms.path) +
                        " --i " +  # Generate INLINE/INLINE_ORIGIN records
                        windows_path(binary.path) +
                        " > " +
                        windows_path(output_file.path),
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
            default = Label("@com_google_breakpad//:dump_syms"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
            doc = "The dump_syms executable. Defaults to @com_google_breakpad//:dump_syms.",
        ),
        "_target_windows": attr.label(default = "@platforms//os:windows"),
    },
)

def substitute_package_variables(ctx, attribute_value):
    """Substitute package_variables in the attribute with the given name.

    Note: This was taken from
    https://github.com/bazelbuild/rules_pkg/blob/main/pkg/private/util.bzl

    Args:
      ctx: context
      attribute_value: the name of the attribute to perform package_variables substitution for

    Returns:
      expanded_attribute_value: new value of the attribute after package_variables substitution
    """
    if not attribute_value:
        return attribute_value

    if type(attribute_value) != "string":
        fail("attempt to substitute package_variables in the attribute value %s which is not a string" % attribute_value)
    vars = dict(ctx.var)
    if ctx.attr.package_variables:
        package_variables = ctx.attr.package_variables[PackageVariablesInfo]
        vars.update(package_variables.values)

    # Map $(var) to {x} and then use format for substitution.
    # This is brittle and I hate it. We should have template substitution
    # in the Starlark runtime.  This loop compensates for mismatched counts
    # of $(foo) so that we don't try replace things like (bar) because we
    # have no regex matching
    for _ in range(attribute_value.count("$(")):
        if attribute_value.find(")") == -1:
            fail("mismatched variable declaration")

        attribute_value = attribute_value.replace("$(", "{", 1)
        attribute_value = attribute_value.replace(")", "}", 1)

    return attribute_value.format(**vars)

def _symbol_zipper_impl(ctx):
    """Packages Breakpad symbol files into a zip archive.

    This rule takes a list of Breakpad symbol files (typically generated by
    the `breakpad_symbols` rule) and packages them into a zip archive
    with a specific directory structure. The structure is:

        <module_name>/<debug_id>/<module_name>.sym

    This layout is required by crash reporting tools that consume Breakpad
    symbols. The `symbol_zipper.py` script is used to perform the zipping
    and structuring.

    Args:
        ctx: The rule context.

    Returns:
        A list containing a `DefaultInfo` provider with the output zip file.
    """

    # setup outputs
    output_name = substitute_package_variables(ctx, ctx.attr.package_file_name)
    default_output = ctx.outputs.out
    output_file = ctx.actions.declare_file(output_name)
    ctx.actions.symlink(output = default_output, target_file = output_file)

    # inputs
    all_inputs = ctx.files.symbols

    # arguments for zip action.
    args = ctx.actions.args()
    args.add("-o", output_file.path)
    args.add_all([input.path for input in all_inputs])

    ctx.actions.run(
        mnemonic = "SymbolZip",
        inputs = all_inputs,
        executable = ctx.executable._symbol_zipper_exe,
        arguments = [args],
        outputs = [output_file],
        env = {
            "LANG": "en_US.UTF-8",
            "LC_CTYPE": "UTF-8",
            "PYTHONIOENCODING": "UTF-8",
            "PYTHONUTF8": "1",
        },
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([output_file]))]

symbol_zipper = rule(
    implementation = _symbol_zipper_impl,
    attrs = {
        "symbols": attr.label_list(
            allow_files = True,
            mandatory = True,
            doc = "The list of symbol files.",
        ),
        "package_file_name": attr.string(doc = "See [Common Attributes](#package_file_name)", mandatory = True),
        "package_variables": attr.label(
            doc = "See [Common Attributes](#package_variables)",
            providers = [PackageVariablesInfo],
        ),
        "out": attr.output(
            doc = """output file name. Default: name + ".zip".""",
            mandatory = True,
        ),
        "_symbol_zipper_exe": attr.label(
            default = Label("//hardware/generic/goldfish/emulator:symbol_zipper"),
            allow_files = True,
            executable = True,
            cfg = "exec",
            doc = "The symbol_zipper executable. Defaults to //hardware/generic/goldfish/emulator:symbol_zipper.",
        ),
    },
)

def breakpad_symbols_pkg(name, binaries, package_file_name, package_variables):
    """Creates a zip file with breakpad symbols.

    This function first extracts symbols from the given binaries using the
    `breakpad_symbols` rule. Then, it uses `pkg_zip` to package the extracted
    symbols into a zip file.

    Args:
        name: The name of the rule.
        binaries: The list of binaries to extract symbols from.
        package_file_name: The name of the output zip file.
        package_variables: A dictionary of variables to be expanded in the
                           package template.
    """
    extract = name + "_extract"
    breakpad_symbols(name = extract, binaries = binaries)
    symbol_zipper(
        name = name,
        out = name + ".zip",
        symbols = [extract],
        package_file_name = package_file_name,
        package_variables = package_variables,
    )

def package_windows_pdbs(name, binaries, package_file_name, package_variables):
    """Packages Windows PDB files into a zip file.

    This function creates a filegroup containing the PDB files associated with the
    specified binaries and then packages them into a zip archive using `pkg_zip`.
    It leverages the `pdb_file` output group to collect the PDB files.

    Args:
        name: The name of the rule.
        binaries: A list of labels representing the binaries for which to
            include PDB files.  These labels should correspond to targets
            that produce PDB files as part of their build process.
        package_file_name: The name of the output zip file.
        package_variables: A dictionary of variables to be expanded in the
                           package template.
    """
    extract = name + "_extract"
    native.filegroup(
        name = extract,
        srcs = binaries,
        output_group = "pdb_file",
    )
    pkg_zip(
        name = name,
        srcs = [
            extract,
        ],
        package_file_name = package_file_name,
        package_variables = package_variables,
    )
