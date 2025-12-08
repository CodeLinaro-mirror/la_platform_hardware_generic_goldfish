load(
    "@goldfish_build//toolchains/cc:rules.bzl",
    "sysroot",
)

package(default_visibility = ["//visibility:public"])

sysroot(
    name = "cts-media-1.4",
    all_files = glob(["android-cts-media-1.4/**"]),
    path = "",
)

sysroot(
    name = "cts-media-1.4-readme",
    all_files = ["android-cts-media-1.4/README.txt"],
    path = "",
)
