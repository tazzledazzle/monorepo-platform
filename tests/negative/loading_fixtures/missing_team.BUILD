load("//tools/build_defs:cc.bzl", "analytics_cc_library")

# EXPECT loading FAIL "missing value for mandatory attribute 'team'" under both `bazel query` and `bazel build`.
analytics_cc_library(
    name = "no_team",
    srcs = ["x.cc"],
    tags = ["manual"],
)
