load("@rules_java//java:java_import.bzl", "java_import")
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_filegroup", "pkg_files")

package(default_visibility = ["//visibility:public"])

java_import(
    name = "tradefed_jars",
    jars = glob(["*.jar"]),
    neverlink = 1,
)

pkg_files(
    name = "android_ets_files",
    srcs = glob(
        ["*"],
        exclude = [
            "*.sh",
            "*bazel*",
        ],
    ),
    prefix = "android-ets/tools",
)

pkg_files(
    name = "android_ets_bin",
    srcs = glob(["*.sh"]),
    attributes = pkg_attributes(mode = "0777"),
    prefix = "android-ets/tools",
)

pkg_filegroup(
    name = "android_ets",
    srcs = [
        ":android_ets_bin",
        ":android_ets_files",
    ],
)
