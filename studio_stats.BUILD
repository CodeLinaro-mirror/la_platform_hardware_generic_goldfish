load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

proto_library(
    name = "model_registry_proto",
    srcs = ["model_registry.proto"],
)

proto_library(
    name = "studio_stats_proto",
    srcs = ["studio_stats.proto"],
    deps = [":model_registry_proto"],
)

cc_proto_library(
    name = "studio_stats_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":studio_stats_proto"],
)

proto_library(
    name = "google_logs_publishing_proto",
    srcs = ["google_logs_publishing.proto"],
)

cc_proto_library(
    name = "google_logs_publishing_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":google_logs_publishing_proto"],
)
