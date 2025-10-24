# Copyright 2025 - The Android Open Source Project

"""rustix bazel build rule"""

load("@rules_rust//rust:defs.bzl", "rust_library")

package(default_visibility = ["//visibility:public"])

rust_library(
    name = "rustix",
    srcs = glob(["src/**/*.rs"]),
    crate_name = "rustix",
    crate_root = "src/lib.rs",
    edition = "2021",
    features = [
        "alloc",
        "event",
        "fs",
        "libc",
        "libc_errno",
        "mm",
        "net",
        "param",
        "pipe",
        "std",
        "use-libc",
        "use-libc-auxv",
    ],
    rustc_flags = [
        "--cfg=libc",
        "--cfg=feature=\"fs\"",
        "--cfg=rustix_no_linux_raw",
        "--cfg=static_assertions",
    ],
    deps = [
        "@bitflags",
        "@errno",
        "@libc",
    ],
)
