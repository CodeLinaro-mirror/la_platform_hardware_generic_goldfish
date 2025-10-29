load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "platform-tools",
    all_files = glob(["platform-tools/**"]),
    path = "",
)

sysroot(
    name = "adb",
    all_files = ["platform-tools/adb"],
    path = "",
)
