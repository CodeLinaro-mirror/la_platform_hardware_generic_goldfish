load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "cts-media-1.5",
    all_files = glob(["android-cts-media-1.5/**"]),
    path = "",
)

sysroot(
    name = "cts-media-1.5-readme",
    all_files = ["android-cts-media-1.5/README.txt"],
    path = "",
)
