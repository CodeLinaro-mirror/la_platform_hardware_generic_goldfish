load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "test_seq_files",
    all_files = glob(["test_seq/**"]),
    path = "",
)

sysroot(
    name = "test_seq",
    all_files = ["test_seq/test_seq"],
    path = "",
)
