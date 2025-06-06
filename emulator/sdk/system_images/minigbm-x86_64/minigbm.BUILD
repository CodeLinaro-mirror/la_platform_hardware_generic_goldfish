load(
    "@//build/bazel/toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "system_image",
    all_files = glob(["x86_64/**"]),
    path = "",
)
