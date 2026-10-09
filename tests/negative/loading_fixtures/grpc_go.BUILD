load("//tools/build_defs:service.bzl", "analytics_grpc_service")

# EXPECT loading FAIL: lang = "go" is not one of ['cc', 'kt'] (bazel query).
analytics_grpc_service(
    name = "go_svc",
    lang = "go",
    main = "main.go",
    tags = ["manual"],
    team = "negative",
)
