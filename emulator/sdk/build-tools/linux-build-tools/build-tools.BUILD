load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "build-tools",
    all_files = glob(["android-Baklava/**"]),
    path = "",
)

sysroot(
    name = "aapt",
    all_files = ["android-Baklava/aapt"],
    path = "",
)
