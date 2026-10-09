# three-languages-one-workspace

A learning sandbox that rebuilds, at small scale, a unified Bazel monorepo build platform: five languages, one build graph, and a shared platform layer that product teams use instead of raw `rules_*`.

**Status: early.** The platform layer (pinned dependencies, 15 wrapper macros, a packaging rule) works and is tested. The product it is meant to build is not written yet. See [Status](#status).

## Why this exists

Tableau migrated 300+ engineers (C++, Java, Kotlin, Python, JavaScript; Windows, macOS, Linux) onto one Bazel build platform, which it calls "Blitz". I wanted to learn what such a migration needs by building a miniature of it, not by reading about it.

The scale is small on purpose. The questions are the real ones:

- How does a platform team enforce policy (warnings as errors, test sizes, ownership tags) without every team copying flags?
- What belongs in `.bazelrc`, and what belongs in a macro?
- When do you write a symbolic macro, and when a rule with a provider?
- What breaks when you adopt Bazel 9 and Bzlmod today?

The target product is a mini analytics system: a C++ query engine, a Java catalog, a Kotlin gateway, a Go ingest agent, and a Python report CLI, all talking over one protobuf/gRPC schema.

This is a portfolio project. It makes no production claims.

## Status

Planning lives in the [beads](docs/planning-in-beads.md) tracker: 17 phases in 4 milestones. Figures below were read from the tracker on 2026-10-09.

| Phase | Milestone | Scope | Status |
|---|---|---|---|
| 1 | M1 | Bootstrap, pinned dependencies, gRPC codegen spike for five languages | Done |
| 2 | M1 | Platform layer: 15 `analytics_*` wrapper macros, `analytics_package` rule | Done |
| 3 | M1 | Proto schema (`analytics.v1`), `analytics_proto` and `analytics_grpc_service` macros | In progress: `.proto` files, compatibility rules (`docs/proto-compatibility.md`) and the `analytics_proto` macro are done, and `//proto/...` builds for C++, Java, Go and Python; `analytics_grpc_service` is built (`tools/build_defs/service.bzl`, cc and kt, tested by `//tools/build_defs/tests/service_fixture/...`) |
| 4 | M1 | C++ query engine | Planned |
| 5 | M1 | JVM services: Java catalog, Kotlin gateway | Planned |
| 6 | M1 | Go ingest agent, Python report CLI | Planned |
| 7 | M1 | `analysistest` suites, negative tests, end-to-end release tests, docs, Gazelle `map_kind` | Planned |
| 8-10 | M2 | Hermetic builds, sanitizers (ASAN/TSAN/UBSAN), local remote cache | Planned |
| 11-14 | M3 | Dependency pinning and conflict checks, affected-target detection, CI, governance (lint, ownership, proto compatibility) | Planned |
| 15-17 | M4 | Platforms and toolchains, Linux cross-compile from macOS, legacy CMake shim | Planned, not yet broken into stories |

Not built: the engine, catalog, gateway, ingest agent, report CLI, release tests, sanitizer configs, remote cache, CI, and cross-compilation. There is no CI, so there is no CI badge.

## What is in the repo

```
.
├── MODULE.bazel, MODULE.bazel.lock   every external dependency, pinned (Bzlmod; no WORKSPACE)
├── .bazelversion, .bazelrc           Bazel 9.3.0; repo-wide toolchain flags
├── go.mod, go.sum                    Go dependencies (via Gazelle go_deps)
├── requirements.in, requirements_lock.txt   Python dependencies
├── maven_install.json                Maven lock file
├── BUILD.bazel                       Gazelle target
├── tools/build_defs/                 the platform layer (built, tested)
│   ├── cc.bzl java.bzl kt.bzl go.bzl py.bzl   wrapper macros
│   ├── common.bzl, jvm.bzl           shared policy and helpers
│   ├── package.bzl, packaging/       analytics_package rule and its tarball tool
│   ├── proto.bzl                     analytics_proto (built)
│   └── tests/                        sample targets and fixtures that exercise the layer
├── proto/analytics/v1/               shared schema: common, ingest, query, catalog .proto (builds for cc, java, go, py)
├── docs/
│   ├── adding-a-target.md            how to write a BUILD file (packaging section only so far)
│   ├── planning-in-beads.md          how the plan is tracked
│   ├── superpowers/specs/            the M1 design
│   └── wiki/                         source for the GitHub wiki
├── .beads/                           issue tracker state
│
│   Planned, not yet present:
├── engine/                           C++ core and gRPC service
├── server/                           Java catalog, Kotlin gateway
├── ingest/agent/                     Go ingest agent
├── reports/cli/                      Python report CLI
├── release/                          package target and end-to-end tests
├── testdata/                         sample CSV and golden output
└── tests/negative/                   targets that must fail to build
```

## Quick start

### Prerequisites

- [Bazelisk](https://github.com/bazelbuild/bazelisk). The repo pins Bazel 9.3.0 in `.bazelversion`; Bazelisk downloads it.
- On macOS, Xcode. `apple_support` is pinned and declared before `rules_cc` so Bazel finds the Xcode toolchain. Without it, Apple clang from Xcode 27 failed on a missing `SDKSettings.json`.
- A network connection for the first build. Bazel fetches its JDKs (the repo targets Java 17 with `remotejdk_17`), a Python 3.12 toolchain, and the Maven, pip, and Go dependencies.

**Tested on macOS arm64 only** (Xcode 27, Apple clang 21). Linux and Windows are untested; the macros avoid platform-specific flags, but I have not run them there.

### Run the platform tests

```sh
bazel test //tools/build_defs/...
```

This runs 7 tests: one per language sample (C++, Java, Kotlin, Go, Python), the tarball tool, and the packaging rule fixture. All pass on my machine.

### Check that packages are reproducible

```sh
tools/build_defs/packaging/check_reproducible.sh
```

The script builds the fixture package in two clean output bases and compares the sha256 of the tarball and manifest. It prints `REPRODUCIBLE` on success. It took about a minute on my machine because each build starts from an empty output base.

## The platform layer

Product teams should load from `//tools/build_defs` only. The layer has two parts.

**Symbolic macros.** Each language has library, binary, and test wrappers, 15 in all. Every wrapper requires a `team` attribute (becomes a `team:<name>` tag), defaults tests to `size = "small"`, and never widens visibility.

| Language | Macros | Policy added |
|---|---|---|
| C++ | `analytics_cc_library`, `_binary`, `_test` | `-Wall -Wextra -Werror` (clang, gcc) or `/W4 /WX` (MSVC) chosen by `select()`; tests link `gtest_main` |
| Java | `analytics_java_library`, `_binary`, `_test` | `-Xlint:all -Werror`; tests get JUnit 4 and a derived `test_class` |
| Kotlin | `analytics_kt_library`, `_binary`, `_test` | Warnings are errors; tests get JUnit 4 and a derived `test_class` |
| Go | `analytics_go_library`, `_binary`, `_test` | `team` tag, test size default |
| Python | `analytics_py_library`, `_binary`, `_test` | Tests run under pytest through one shared shim |

Example, from the sample targets in `tools/build_defs/tests/samples/cc/BUILD.bazel`:

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

And the Java test, where `test_class` is derived from the file path:

```python
analytics_java_test(
    name = "GreeterTest",
    srcs = ["src/test/java/sample/GreeterTest.java"],  # -> sample.GreeterTest
    team = "platform",
    deps = [":lib"],
)
```

**A custom rule.** `analytics_package` bundles binaries and their runfiles into `<name>.tar` and `<name>.MANIFEST.json` (path, sha256, source label per file). It returns an `AnalyticsPackageInfo` provider so packages nest, and it fails the build when two files map to one install path. Both outputs are byte-reproducible. `rules_pkg` is the production choice; this rule exists to practice providers, depsets, runfiles, and exec-configured tools.

**Policy split.** Flags that affect the toolchain or compatibility (`-std=c++20`, Java 17) live in `.bazelrc`, where they take part in cache keys. Per-target policy (warnings, test dependencies, tags) lives in macros.

Details: [Platform macros](docs/wiki/Platform-Macros.md), [Packaging rule](docs/wiki/Packaging-Rule.md), [Adding a target](docs/adding-a-target.md).

## How planning works

The project plans in [beads](docs/planning-in-beads.md) (`bd`) instead of a `.planning/` directory. A phase is an epic with a goal and acceptance criteria. A phase closes with a comment citing evidence, and that unblocks the next one. Useful commands:

```sh
bd stats           # counts
bd ready           # unblocked work
bd show tlow-049   # the project epic
bd list --label phase:3
```

## Design decisions and findings

The full list, with fixes, is in [Findings and gotchas](docs/wiki/Findings-and-Gotchas.md). The short version:

- **Bazel 9 removed the native `cc_*` and `java_*` rules.** Every BUILD file and macro needs an explicit `load()`.
- **`macro(inherit_attrs = ...)` accepts only a rule or macro symbol.** `rules_cc`, `rules_java`, and `rules_python` export their rules as plain functions, and so does `rules_go`'s `go_binary`. Those wrappers list attributes explicitly. `kt_jvm_*`, `go_library`, and `go_test` are real rules and inherit cleanly.
- **A macro that inspects `srcs` needs it non-configurable.** The JVM test wrappers derive `test_class` from `srcs`, so `srcs` cannot be a `select()` there.
- **grpc-java and protobuf both contribute to `@maven`.** The root module must set `known_contributing_modules` and add `animal-sniffer-annotations`, or analysis fails.
- **Go needs two proto compilers**, `go_proto` and `go_grpc_v2`. With `go_grpc_v2` alone the message types are undefined.
- **A cold C++ plus Java gRPC build took 215 s (4,598 actions)** in the spike. That is the baseline for the planned cache work.

## Roadmap

Four milestones, each with its own spec, plan, and working result:

| Milestone | Theme |
|---|---|
| M1 | Layout, mini analytics product, platform macros, shared proto, packaging |
| M2 | Hermeticity, sanitizers, local remote cache |
| M3 | Dependency pinning, affected-target CI, governance |
| M4 | Platforms, toolchains, cross-compilation, legacy CMake shim |

Phases, gates, and status: [Roadmap](docs/wiki/Roadmap.md).

## Contributing and working notes

- This is a personal project; there is no contribution process yet.
- Work from the tracker: `bd ready`, claim a task, close it with evidence.
- Do not hard-code absolute paths or macOS-only flags in macros or BUILD files. M4 depends on that.
- Put per-user Bazel flags in `user.bazelrc`; it is git-ignored and imported by `.bazelrc`.
- Run `bazel test //tools/build_defs/...` before sending a change to the platform layer.

## Documentation

- Design spec: [`docs/superpowers/specs/2026-10-09-m1-monorepo-layout-macros-design.md`](docs/superpowers/specs/2026-10-09-m1-monorepo-layout-macros-design.md)
- [Adding a target](docs/adding-a-target.md) (partial)
- [Planning in beads](docs/planning-in-beads.md)
- [Proto compatibility rules](docs/proto-compatibility.md)
- Wiki source, in [`docs/wiki/`](docs/wiki/README.md): [Home](docs/wiki/Home.md), [Getting started](docs/wiki/Getting-Started.md), [Architecture](docs/wiki/Architecture.md), [Platform macros](docs/wiki/Platform-Macros.md), [Packaging rule](docs/wiki/Packaging-Rule.md), [Adding a target](docs/wiki/Adding-a-Target.md), [Roadmap](docs/wiki/Roadmap.md), [Findings and gotchas](docs/wiki/Findings-and-Gotchas.md), [Glossary](docs/wiki/Glossary.md)

## License

Not yet chosen.
