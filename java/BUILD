# java/BUILD
load("@rules_java//java:java_library.bzl", "java_library")  # Bazel 9 removed the native java_* rules
load("@rules_java//java:java_test.bzl", "java_test")

java_library(
    name = "lib",
    srcs = glob(["src/main/java/**/*.java"]),  # glob keeps srcs in sync with files on disk
)

java_test(
    name = "lib_test",
    srcs = ["LibTest.java"],
    test_class = "LibTest",  # which class the runner executes
    deps = [
        ":lib",
        "@maven//:junit_junit",  # JUnit 4, fetched by rules_jvm_external
    ],
)
