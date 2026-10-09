# cpp/BUILD
load("@rules_cc//cc:cc_library.bzl", "cc_library")  # Bazel 9 removed the native cc_* rules
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "greeter",
    srcs = ["greeter.cc"],
    hdrs = ["greeter.h"],  # public header; others depend on this
    visibility = ["//visibility:public"],
)

cc_test(
    name = "greeter_test",
    srcs = ["greeter_test.cc"],
    deps = [":greeter"],  # a header not covered by deps fails under sandbox
)
