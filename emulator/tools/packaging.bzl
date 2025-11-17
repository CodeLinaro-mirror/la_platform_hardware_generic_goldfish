"""Bazel rules and macros for packaging."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@goldfish_build//rules/native:native_binaries.bzl", "TransformedFilesInfo", "native_symbols")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes")
load("@rules_pkg//pkg:providers.bzl", "PackageFilesInfo", "PackageVariablesInfo")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")
load(":breakpad_symbols.bzl", "breakpad_symbols")

visibility("//emulator/...")

def _aemu_naming_impl(ctx):
    """Implementation function for the `aemu_naming` rule.

    This function collects relevant information about the build target and
    its environment to create a structured set of variables. These variables
    can be used to dynamically generate package names or other artifacts.

    Args:
        ctx: The rule context object, providing access to attributes,
            configuration, and build environment information.

    Returns:
        PackageVariablesInfo: A provider object containing a dictionary of
            variables relevant for naming.
    """
    values = {}

    # Copy attributes from the rule to the provider
    values["product_name"] = ctx.attr.product_name
    values["version"] = ctx.attr.version
    values["revision"] = ctx.attr.revision
    values["platform"] = ctx.attr.platform

    # Add some well known variables from the rule context.
    values["target_cpu"] = ctx.var.get("TARGET_CPU")
    values["compilation_mode"] = ctx.var.get("COMPILATION_MODE")

    build_id_dep = ctx.attr.build_id_dep[PackageVariablesInfo]
    values["build_id"] = build_id_dep.values["build_id"]
    return PackageVariablesInfo(values = values)

#
# A rule to inject variables from the build file into package names.
#
aemu_naming = rule(
    implementation = _aemu_naming_impl,
    # build_setting = config.string(flag = True),
    attrs = {
        "product_name": attr.string(
            default = "Android Emulator",
            doc = "Placeholder for our final product name.",
        ),
        "revision": attr.string(
            doc = "Placeholder for our release revision.",
        ),
        "version": attr.string(
            default = "99.1.1",
            doc = "Placeholder for our release version.",
        ),
        "platform": attr.string(
            doc = "The target operating system of this release",
        ),
        "build_id_dep": attr.label(
            providers = [PackageVariablesInfo],
        ),
        "build_id": attr.string(
            doc = "The build id of this release",
        ),
    },
)

def _build_id_from_cmdline_impl(ctx):
    """Implementation function for the `build_id_from_command_line` rule.

    This function simply packages the build ID provided on the command line
    as a `PackageVariablesInfo` provider.

    Args:
        ctx: The rule context object, providing access to the build setting
            value.

    Returns:
        PackageVariablesInfo: A provider containing the build ID value.
    """
    values = {"build_id": ctx.build_setting_value}

    # Just pass the value from the command line through. An implementation
    # could also perform validation, such as done in
    # https://github.com/bazelbuild/bazel-skylib/blob/master/rules/common_settings.bzl
    return PackageVariablesInfo(values = values)

build_id_from_command_line = rule(
    implementation = _build_id_from_cmdline_impl,
    # Note that the default value comes from the rule instantiation.
    build_setting = config.string(flag = True),
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
            default = Label("@goldfish//emulator/tools:symbol_zipper"),
            allow_files = True,
            executable = True,
            cfg = "exec",
            doc = "The symbol_zipper executable. Defaults to @goldfish//emulator/tools:symbol_zipper.",
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

def native_symbols_pkg(name, binaries, package_file_name, package_variables, layout_templates = {}):
    """Creates a zip file with native symbols.

    This function first extracts symbols from the given binaries using the
    `native_symbols` rule, and then packages them into a zip archive using
    `pkg_zip`.

    Args:
        name: The name of the rule.
        binaries: A list of labels representing the binaries for which to
            include native symbols. These labels should correspond to targets
            that produce native symbols as part of their build process or the
            attached aspects.
        layout_templates: A dict from layout names to pkg_files targets served
            as templates for package layout. This is an optional argument.
        package_file_name: The name of the output zip file.
        package_variables: A dictionary of variables to be expanded in the
                           package template.
    """
    extract = name + "_extract"
    native_symbols(
        name = extract,
        srcs = binaries,
    )
    pkg_files = []
    for layout_name, template in layout_templates.items():
        remap_name = name + "_layout_" + layout_name
        pkg_files.append(remap_name)
        remapped_pkg_files(
            name = remap_name,
            src = template,
            transform = extract,
            keep_transformed_only = True,
            attributes = pkg_attributes(
                mode = "0644",
            ),
        )
    if not layout_templates:
        pkg_files.append(extract)
    pkg_zip(
        name = name,
        srcs = pkg_files,
        package_file_name = package_file_name,
        package_variables = package_variables,
    )

def _stem_and_extensions(filename):
    # type: (string) -> tuple[string, list[string]]
    segs = filename.split(".")
    stem = segs[0]
    exts = segs[1:]
    if filename.startswith("."):
        stem = "." + segs[1]
        exts = segs[2:]
    return stem, exts

def _prefix_and_suffix(from_str, to_str):
    # type: (string, string) -> tuple[string, string]
    pos = to_str.find(from_str)
    if pos != -1:
        return to_str[:pos], to_str[pos + len(from_str):]
    return "", ""

# TODO(b/397510455) add test
def _find_filename_changes(from_name, to_name):
    # type: (string, string) -> struct
    from_stem, from_exts = _stem_and_extensions(from_name)
    to_stem, to_exts = _stem_and_extensions(to_name)

    add_stem_prefix, add_stem_suffix = _prefix_and_suffix(from_stem, to_stem)
    rm_stem_prefix, rm_stem_suffix = _prefix_and_suffix(to_stem, from_stem)
    if not any([add_stem_prefix, add_stem_suffix, rm_stem_prefix, rm_stem_suffix]) and from_stem != to_stem:
        fail("Cannot find changes between", from_stem, "and", to_stem, ": they seem distinct")

    for pos in range(min([len(from_exts), len(to_exts)])):
        if from_exts[pos] == to_exts[pos]:
            from_exts.pop(0)
            to_exts.pop(0)
    rm_extensions = from_exts
    add_extensions = to_exts

    changes = struct(
        add_stem_prefix = add_stem_prefix,
        add_stem_suffix = add_stem_suffix,
        rm_stem_prefix = rm_stem_prefix,
        rm_stem_suffix = rm_stem_suffix,
        rm_extensions = rm_extensions,
        add_extensions = add_extensions,
    )
    return changes

# TODO(b/397510455) add test
def _apply_filename_changes(changes, path):
    directory = paths.dirname(path)
    basename = paths.basename(path)

    stem, exts = _stem_and_extensions(basename)
    stem = stem.removeprefix(changes.rm_stem_prefix).removesuffix(changes.rm_stem_suffix)
    stem = changes.add_stem_prefix + stem + changes.add_stem_suffix

    for ext in reversed(changes.rm_extensions):
        if exts and exts[-1] == ext:
            exts.pop()
        else:
            fail("Cannot remove extensions", changes.rm_extensions, "from file", path)
    exts.extend(changes.add_extensions)
    basename = ".".join([stem] + exts)
    return paths.join(directory, basename)

def _remapped_pkg_files_impl(ctx):
    dest_src_map = ctx.attr.src[PackageFilesInfo].dest_src_map  # type: dict[string, File]
    transform = ctx.attr.transform[TransformedFilesInfo].mapping  # type: dict[File, list[File]]
    transform_original = ctx.attr.transform[TransformedFilesInfo].original or {}  # type: dict[File, File]
    transformed_map = {}  # type: dict[string, File]
    for dest_path, src_file in dest_src_map.items():
        if src_file in transform:
            for transformed_file in transform[src_file]:
                name_change = _find_filename_changes(
                    transform_original.get(transformed_file, src_file).basename,
                    transformed_file.basename,
                )
                transformed_dest = _apply_filename_changes(name_change, dest_path)
                transformed_map[transformed_dest] = transformed_file
        elif not ctx.attr.keep_transformed_only:
            transformed_map[dest_path] = src_file

    attributes = dict(ctx.attr.src[PackageFilesInfo].attributes)
    if ctx.attr.attributes:
        attributes.update(json.decode(ctx.attr.attributes))

    return [
        PackageFilesInfo(
            dest_src_map = transformed_map,
            attributes = attributes,
        ),
        DefaultInfo(
            files = depset(transformed_map.values()),
        ),
    ]

remapped_pkg_files = rule(
    implementation = _remapped_pkg_files_impl,
    doc = """Remap pkg_files by applying transformations.

    For a pkg_files target with the following mapping:

      a -> release/a.out
      b.exe -> test/c.exe

    And suppose we have transformed files "a" and "b.exe":

      a -> a.signed, a.wrapper
      b.exe -> b.pdb, libb.dll

    This rule creates a pkg_files-compatible mapping with the following contents:

      a.signed -> release/a.out.signed
      a.wrapper -> release/a.out.wrapper
      b.pdb -> test/c.pdb
      libb.dll -> test/libc.dll

    Specifically, this rule handles the following filename changes during the
    transformation:

      * Remove extension
      * Add extension
      * Add prefix / suffix to stem OR remove prefix / suffix from stem
    """,
    attrs = {
        "src": attr.label(
            doc = "A pkg_files target to remap.",
            mandatory = True,
            providers = [PackageFilesInfo],
        ),
        "transform": attr.label(
            doc = "Transformation to apply. This is a target that returns TransformedFilesInfo",
            mandatory = True,
            providers = [TransformedFilesInfo],
        ),
        "keep_transformed_only": attr.bool(
            doc = "Remove map entries that are not transformed. Otherwise they are copied over from src.",
        ),
        "attributes": attr.string(
            doc = "Override attributes from src. See https://bazelbuild.github.io/rules_pkg/latest.html#pkg_files-attributes.",
        ),
    },
)
