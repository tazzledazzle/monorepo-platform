# Adding a Target

The authoritative guide is `docs/adding-a-target.md` in the repository. It is partial today: the packaging section is written, and the per-language sections arrive with the phases that need them. This page does not repeat it. It shows the common pattern so a team can start.

## The rule

Product BUILD files load only from `//tools/build_defs`, never from `@rules_cc`, `@rules_java`, and so on. Today this is a convention. A lint that enforces it is planned (Phase 14).

## Steps

1. Pick the macro for your language from [Platform Macros](Platform-Macros).
2. `load()` it from the matching `.bzl` file.
3. Set `team` (required). Set `visibility` only as wide as callers need. The default is private.
4. Put tests next to the library. They default to `size = "small"`.
5. Run `bazel test //your/package:all`.

## C++

```python
load("//tools/build_defs:cc.bzl", "analytics_cc_library", "analytics_cc_test")

analytics_cc_library(
    name = "lib",
    srcs = ["lib.cc"],
    hdrs = ["lib.h"],
    team = "platform",
)

analytics_cc_test(
    name = "lib_test",
    srcs = ["lib_test.cc"],
    team = "platform",
    deps = [":lib"],
)
```

Warnings are errors, so an unused variable fails the build. Do not add gtest; the test macro links it.

## Java and Kotlin

```python
load("//tools/build_defs:java.bzl", "analytics_java_library", "analytics_java_test")

analytics_java_library(
    name = "lib",
    srcs = ["src/main/java/sample/Greeter.java"],
    team = "platform",
)

analytics_java_test(
    name = "GreeterTest",
    srcs = ["src/test/java/sample/GreeterTest.java"],
    team = "platform",
    deps = [":lib"],
)
```

Name the test target after the test file (`GreeterTest` for `GreeterTest.java`). The macro derives `sample.GreeterTest` from the path. Set `test_class` to override. Kotlin works the same way with `kt.bzl`.

## Python

```python
load("//tools/build_defs:py.bzl", "analytics_py_library", "analytics_py_test")

analytics_py_library(name = "lib", srcs = ["lib.py"], team = "platform")

analytics_py_test(
    name = "lib_test",
    srcs = ["lib_test.py"],
    team = "platform",
    deps = [":lib"],
)
```

Write plain pytest files. Do not provide a `main`.

## Go

```python
load("//tools/build_defs:go.bzl", "analytics_go_library", "analytics_go_test")
```

Attributes match `go_library` and `go_test`. Gazelle generates these calls through `map_kind` (see the root `BUILD.bazel`). It cannot invent the required `team`, so add that attribute by hand after generating.

## Packaging

See [Packaging Rule](Packaging-Rule), and `docs/adding-a-target.md` for the canonical snippet.

## When a macro rejects an attribute

The C++, Java, Python, and `go_binary` wrappers list attributes explicitly. If you need one that is missing, add it to the attribute dictionary in the `.bzl` file and open a change to the platform layer. See [Findings and Gotchas](Findings-and-Gotchas).

## Services from a proto contract

`analytics_proto` generates stubs for four languages (see `proto/analytics/v1/BUILD.bazel`); `analytics_grpc_service` builds proto-backed services, as in `//engine/service` and `//server/gateway`. A real Kotlin example from `tools/build_defs/tests/service_fixture/kt/BUILD.bazel`:

```python
analytics_grpc_service(
    name = "svc",
    lang = "kt",                      # "cc" or "kt"
    team = "platform",
    srcs = glob(["src/main/kotlin/**/*.kt"]),
    test_srcs = glob(["src/test/kotlin/**/*.kt"]),
    protos = ["//tools/build_defs/tests/proto_fixture/service"],  # the analytics_proto name, not a generated target
    main = "fixture.MainKt",          # kt: main class; cc: the file with main()
)
# -> :svc_lib, :svc (binary), :svc_test.  Run: bazel test //path:svc_test
```
