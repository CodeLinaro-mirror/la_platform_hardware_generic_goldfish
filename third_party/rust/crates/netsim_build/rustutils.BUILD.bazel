# Copyright 2025 - The Android Open Source Project

"""rustutils bazel build rule"""

load("@rules_rust//rust:defs.bzl", "rust_library")

package(default_visibility = ["//visibility:public"])

rust_library(
    name = "rustutils",
    srcs = glob(["src/**/*.rs"]),
    crate_name = "rustutils",
    crate_root = "src/lib.rs",
    edition = "2021",
    proc_macro_deps = [
        "@num-derive",
    ],
    rustc_env = {"OUT_DIR": ".."},
    deps = [
        "@anyhow",
        "@libc",
        "@log",
        "@nix",
        "@thiserror",
    ],
)
