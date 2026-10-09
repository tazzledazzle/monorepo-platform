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

Attributes match `go_library` and `go_test`. Gazelle should eventually generate these calls through `map_kind`. That configuration is not in the repo yet (Phase 7), so write them by hand for now.

## Packaging

See [Packaging Rule](Packaging-Rule), and `docs/adding-a-target.md` for the canonical snippet.

## When a macro rejects an attribute

The C++, Java, Python, and `go_binary` wrappers list attributes explicitly. If you need one that is missing, add it to the attribute dictionary in the `.bzl` file and open a change to the platform layer. See [Findings and Gotchas](Findings-and-Gotchas).

## Not yet available

`analytics_proto` and `analytics_grpc_service` (Phase 3, in progress) will cover proto-backed services.
