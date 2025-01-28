"""Bazel rules for extracting and packaging debug symbols."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

def _extract_symbols_impl(ctx):
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
    for binary in ctx.files.binaries:
        owner_label = binary.owner
        output_name = "/".join([
            owner_label.package,
            paths.replace_extension(binary.basename, ".sym"),
        ])
        if owner_label.repo_name:
            output_name = "_" + owner_label.repo_name + "/" + output_name
        output_file = ctx.actions.declare_file(output_name)
        output_files.append(output_file)

        ctx.actions.run(
            outputs = [output_file],
            inputs = [binary],  # Simplified: directly use binary
            executable = ctx.executable.dump_syms,
            arguments = [
                "-d",  # Generate INLINE/INLINE_ORIGIN records
                "-m",  # Handle multiple symbols at same address, if any.
                "-f",  # Output to:
                output_file.path,
                binary.path,  # Simplified: directly use binary.path
            ],
        )

    return DefaultInfo(files = depset(output_files))

# Define the rule
extract_symbols = rule(
    implementation = _extract_symbols_impl,
    attrs = {
        "binaries": attr.label_list(
            allow_files = True,
            mandatory = True,
            doc = "The list of binaries to extract symbols from.",
        ),
        "dump_syms": attr.label(
            default = Label("@com_google_breakpad//:dump_syms"),
            allow_single_file = True,
            executable = True,
            cfg = "exec",
            doc = "The dump_syms executable. Defaults to @com_google_breakpad//:dump_syms.",
        ),
    },
)

def package_symbols(name, binaries, package_file_name, package_variables):
    """Packages symbols into a zip file.

    This function first extracts symbols from the given binaries using the
    `extract_symbols` rule. Then, it uses `pkg_zip` to package the extracted
    symbols into a zip file.

    Args:
        name: The name of the rule.
        binaries: The list of binaries to extract symbols from.
        package_file_name: The name of the output zip file.
        package_variables: A dictionary of variables to be expanded in the
                           package template.
    """
    extract = name + "_extract"
    extract_symbols(name = extract, binaries = binaries)
    pkg_zip(
        name = name,
        srcs = [
            extract,
        ],
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
