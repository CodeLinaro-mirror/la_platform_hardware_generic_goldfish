"""Macro for cc_binary targets that embeds a UTF-8 activeCodePage manifest on Windows."""

load("@goldfish_build//rules:windows_resources.bzl", "windows_manifest_resource")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

def cc_binary_with_manifest(name, deps = [], testonly = False, **kwargs):
    """Defines a cc_binary with an embedded Windows UTF-8 activeCodePage manifest.

    Args:
        name: The name of the cc_binary target.
        deps: Dependencies of the cc_binary target.
        testonly: Whether the target is testonly.
        **kwargs: Additional arguments forwarded to cc_binary.
    """
    manifest_rc_name = name.replace("-", "_") + "_manifest_rc"
    windows_manifest_resource(
        name = manifest_rc_name,
        testonly = testonly,
        binary_filename = name + ".exe",
        target_compatible_with = ["@platforms//os:windows"],
    )
    cc_binary(
        name = name,
        testonly = testonly,
        deps = deps + select({
            "@platforms//os:windows": [":" + manifest_rc_name],
            "//conditions:default": [],
        }),
        **kwargs
    )
