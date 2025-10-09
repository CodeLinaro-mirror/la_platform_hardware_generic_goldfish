# Copyright 2023 The Android Open Source Project

"""Type erasure for async trait methods"""

load("@rules_rust//rust:defs.bzl", "rust_proc_macro")

package(default_visibility = ["//visibility:public"])

rust_proc_macro(
    name = "async-trait",
    srcs = glob(["**/*.rs"]),
    crate_name = "async_trait",
    crate_root = "src/lib.rs",
    edition = "2021",
    deps = [
        "@proc-macro2",
        "@quote",
        "@syn",
    ],
)
