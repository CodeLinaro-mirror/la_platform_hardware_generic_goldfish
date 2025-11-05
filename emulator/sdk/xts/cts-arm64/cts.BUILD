load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "cts",
    all_files = glob(["android-cts/**"]),
    path = "",
)

sysroot(
    name = "cts-tradefed",
    all_files = ["android-cts/tools/cts-tradefed"],
    path = "",
)
