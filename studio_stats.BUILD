load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "studio_stats_proto",
    srcs = ["studio_stats.proto"],
)

cc_proto_library(
    name = "studio_stats_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":studio_stats_proto"],
)
