load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_license//rules:license.bzl", "license")
load("@rules_license//rules:license_kind.bzl", "license_kind")
load("@rules_rust//rust:defs.bzl", "rust_library", "rust_shared_library")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    license_kinds = [
        ":SPDX-license-identifier-Apache-2.0",
        ":SPDX-license-identifier-MIT",
    ],
    license_text = "LICENSE-APACHE",
    visibility = [":__subpackages__"],
)

license_kind(
    name = "SPDX-license-identifier-Apache-2.0",
    conditions = ["notice"],
    url = "https://spdx.org/licenses/Apache-2.0.html",
)

license_kind(
    name = "SPDX-license-identifier-MIT",
    conditions = ["notice"],
    url = "",
)

rust_library(
    name = "rutabaga_gfx",
    srcs = glob(["rutabaga_gfx/**/*.rs"]),
    crate_features = [
        "gfxstream",
    ],
    edition = "2021",
    proc_macro_deps = [
        "@remain",
        "@thiserror-impl",
    ],
    deps = [
        "@cfg-if",
        "@gfxstream//host:gfxstream_backend_shared",
        "@libc",
        "@log",
        "@serde",
        "@serde_json",
        "@thiserror",
        "@zerocopy",
    ] + select({
        "@platforms//os:windows": [
            "@winapi",
            "@winapi-x86_64-pc-windows-gnu",
        ],
        "@platforms//os:linux": ["@nix"],
        "//conditions:default": [],
    }),
)

CRATE_FEATURES = [
    "rutabaga_gfx/gfxstream",
]

rust_shared_library(
    name = "rutabaga_ffi",
    srcs = ["rutabaga_gfx/ffi/src/lib.rs"],
    crate_features = CRATE_FEATURES,
    edition = "2021",
    deps = [
        ":rutabaga_gfx",
        "@//external/qemu:libmodule-common",
        "@gfxstream//host:gfxstream_backend",
        "@libc",
        "@log",
        "@once_cell",
    ] + select({
        "@platforms//os:linux": ["@nix"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "rutabaga_c_ffi",
    hdrs = ["rutabaga_gfx/ffi/src/include/rutabaga_gfx_ffi.h"],
    include_prefix = "rutabaga_gfx",
    linkopts = select({
        "@platforms//os:windows": ["/DEFAULTLIB:Ntdll.lib"],
        "//conditions:default": [],
    }),
    strip_include_prefix = "rutabaga_gfx/ffi/src/include",
    deps = [":rutabaga_ffi"],
)
