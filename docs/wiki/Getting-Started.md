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
bazel test //...
```

34 tests pass on my machine. They fall into these groups:

| Group | Targets | What they check |
|---|---|---|
| Product unit tests | `//engine/core:table_test`, `:aggregate_test`, `//engine/service:engine_test`, `//server/catalog:CatalogTest`, `//server/gateway:gateway_test`, `//ingest/agent:agent_test`, `//ingest/agent/main:main_test`, `//reports/cli:report_lib_test` | Each component alone |
| Platform samples | `//tools/build_defs/tests/samples/{cc,java,kt,go,py}` (5 tests) | One wrapper per language |
| Platform fixtures | `//tools/build_defs/packaging:make_tarball_test`, `//tools/build_defs/tests/package_fixture:package_rule_test`, `//tools/build_defs/tests/service_fixture/{cc,kt}:svc_test` | The tarball tool, `analytics_package`, `analytics_grpc_service` |
| Analysis tests | `//tools/build_defs/tests/analysis:*` (15 tests) | Tags, flags, providers and defaults the macros and the rule produce |
| Release tests | `//release:smoke_test`, `//release:package_test` | Whole product, whole package |

## Run the negative tests

```sh
bash scripts/check_negative.sh
```

Ten cases. Each builds a target under `tests/negative/` (or a temporary fixture) and checks for a specific error message: a C++, Java, or Kotlin warning under `-Werror`, a visibility breach, a missing `team`, an unsupported `lang`. One case, `legacy-hdrs-leak`, expects success, to show what a classic macro permits. All ten pass. They are a script because a visibility or loading error stops the BUILD file from loading.

## Run the product

Build the four binaries:

```sh
bazel build //engine/service:engine //server/gateway:gateway \
  //ingest/agent/main:agent //reports/cli:cli
```

Start the engine, then the gateway. `--port=0` picks a free port and each server prints `LISTENING <port>` once it accepts calls:

```sh
bazel-bin/engine/service/engine --port=0 &
bazel-bin/server/gateway/gateway --engine=127.0.0.1:<engine-port> --port=0 &
```

Load the sample CSV and ask for a report:

```sh
bazel-bin/ingest/agent/main/agent_/agent \
  --engine=127.0.0.1:<engine-port> --dataset=sales testdata/sales.csv
# loaded 10 rows into sales

bazel-bin/reports/cli/cli \
  --gateway=127.0.0.1:<gateway-port> --dataset=sales --agg=sum:revenue --group-by=region
```

The output is the golden file `testdata/sales_by_region.golden`:

```
region      sum(revenue)
--------  --------------
east              195.75
north              94.05
south              170.5
west              102.25
```

An unknown dataset gives `NOT_FOUND` and an unknown column gives `INVALID_ARGUMENT`, each as one stderr line with exit code 1. Stop both servers with `kill`.

The engine binds `127.0.0.1` by default (`--host` changes it). The gateway binds all interfaces and has no `--host` flag. `//release:smoke_test` does this whole sequence, with cleanup, under `TEST_TMPDIR`.

## Build the package

```sh
bazel build //release:analytics
```

This produces `analytics.tar` (about 308 MB, of which about 190 MB is the Python CLI's runfiles) and `analytics.MANIFEST.json`. `//release:package_test` unpacks it and checks every manifest checksum. The packaged Python CLI does not run from the unpacked tarball; see [Packaging Rule](Packaging-Rule).

## Check reproducibility

```sh
tools/build_defs/packaging/check_reproducible.sh
```

Builds a fixture package in two clean output bases and compares checksums. Exit 0 means identical. It took about a minute here.

## Clean-build time

Clean-build baseline: **459 s (7 min 39 s)** for `bazel test //...` from a fresh clone with a new, empty Bazel output base: 8,559 actions (5,108 sandboxed, 65 worker), all 34 tests executed, critical path 60 s. Measured 2026-10-09 on macOS arm64 (Xcode 27) at commit `be4c559`. Bazel's download cache was warm, so a first-ever build on a new machine also pays for fetching dependencies. This is the number M2's remote cache is measured against.

## Per-user flags

Put personal Bazel flags in `user.bazelrc` at the repo root. `.bazelrc` imports it and git ignores it.

## What you cannot run yet

CI, sanitizer builds, a remote cache, hermetic-build checks, and cross-compilation do not exist (M2-M4). See [Roadmap](Roadmap).

Next: [Architecture](Architecture).
