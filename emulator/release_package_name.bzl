"""
This Starlark Bazel (`.bzl`) file provides functionality for managing build
variables and injecting them into package names.
"""

load("@rules_pkg//pkg:providers.bzl", "PackageVariablesInfo")

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
