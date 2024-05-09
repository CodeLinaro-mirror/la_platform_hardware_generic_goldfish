load("@rules_license//rules:license.bzl", "license")
load("@rules_license//rules:license_kind.bzl", "license_kind")
load("@rules_rust//rust:defs.bzl", "rust_library")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    license_kinds = [
        ":SPDX-license-identifier-MIT",
    ],
    license_text = "LICENSE",
    visibility = [":__subpackages__"],
)

license_kind(
    name = "SPDX-license-identifier-MIT",
    conditions = ["notice"],
    url = "",
)

rust_library(
    name = "nix",
    srcs = glob(
        include = ["**/*.rs"],
        exclude = [
            "build.rs",
            "test/sys/test_aio_drop.rs",
            "test/sys/test_prctl.rs",
            "test/test.rs",
            "test/test_clearenv.rs",
        ],
    ),
    crate_features = [
        "default",
        "event",
        "eventfd",
        "feature",
        "fs",
        "ioctl",
        "memoffset",
        "mman",
        "socket",
        "uio",
    ],
    edition = "2021",
    rustc_flags = [
        "--cap-lints=allow",
        "--cfg=linux",
        "--cfg=linux_android",
    ],
    deps = [
        "@bitflags",
        "@cfg-if",
        "@libc",
        "@memoffset",
    ],
)
