# Getting Started

## Prerequisites

- [Bazelisk](https://github.com/bazelbuild/bazelisk). `.bazelversion` pins Bazel 9.3.0.
- macOS: Xcode. `apple_support` 2.10.0 is declared before `rules_cc` in `MODULE.bazel` so Bazel uses the Xcode-aware C++ toolchain. Without it, Apple clang from Xcode 27 failed because the toolchain did not know the SDK's `SDKSettings.json`.
- Network access on first build. Bazel fetches its JDKs (the repo targets Java 17 with `remotejdk_17`), a Python 3.12 toolchain, and Maven, pip, and Go dependencies.
- You do not install a JDK, Python, or Go yourself.

## Tested platforms

macOS arm64 with Xcode 27 and Apple clang 21. Nothing else has been run. The macros pick compiler flags with `select()` and hard-code no platform paths, which should help on Linux, but that is a design intent, not a result.

## Run the tests

```sh
bazel test //tools/build_defs/...
```

Seven tests run:

| Test | What it checks |
|---|---|
| `//tools/build_defs/tests/samples/cc:lib_test` | C++ wrapper, gtest linked in |
| `//tools/build_defs/tests/samples/java:GreeterTest` | Java wrapper, derived `test_class` |
| `//tools/build_defs/tests/samples/kt:GreeterTest` | Kotlin wrapper |
| `//tools/build_defs/tests/samples/go:g_test` | Go wrapper |
| `//tools/build_defs/tests/samples/py:lib_test` | Python wrapper, pytest shim |
| `//tools/build_defs/packaging:make_tarball_test` | the tarball tool |
| `//tools/build_defs/tests/package_fixture:package_rule_test` | `analytics_package` end to end |

## Check reproducibility

```sh
tools/build_defs/packaging/check_reproducible.sh
```

Builds a fixture package in two clean output bases and compares checksums. Exit 0 means identical. It took about a minute here.

## Per-user flags

Put personal Bazel flags in `user.bazelrc` at the repo root. `.bazelrc` imports it and git ignores it.

## What you cannot run yet

There is no product to run. The repository contains the platform layer, its samples and tests, and in-progress proto work. The engine, gateway, agent, and CLI do not exist yet. See [Roadmap](Roadmap).

Next: [Architecture](Architecture).
