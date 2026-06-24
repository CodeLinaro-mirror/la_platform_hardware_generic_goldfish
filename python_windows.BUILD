# Build file for prebuilt Windows Python interpreter.
# This makes the python binary and its library files available as a tool in Bazel actions.

exports_files(["python.exe"])

filegroup(
    name = "python_bin",
    srcs = glob(["**/*"]),
    visibility = ["//visibility:public"],
)
