# Architecture

This page describes the layout built in milestone M1, against the design in `docs/superpowers/specs/2026-10-09-m1-monorepo-layout-macros-design.md`. For the repository's own design overview, see `docs/architecture.md`. Everything below is built; M2-M4 work is on the [Roadmap](Roadmap).

## Layout

| Path | Owner team | Contents | Status |
|---|---|---|---|
| `MODULE.bazel`, `.bazelversion`, `.bazelrc` | platform | Pinned dependencies, Bazel 9.3.0, shared flags | built |
| `go.mod`, `requirements.in`, `maven_install.json` and lock files | platform | Per-ecosystem dependency locks | built |
| `tools/build_defs/` | platform | Wrapper macros, `analytics_package`, shared policy | built |
| `tools/build_defs/proto.bzl`, `proto/analytics/v1/` | platform | `analytics_proto` macro; `common`, `ingest`, `query`, `catalog` `.proto` files (`//proto/...` builds for cc, java, go, py; see `docs/proto-compatibility.md`) | built |
| `engine/core` | engine (C++) | Columnar table and aggregations; no proto, no gRPC | built |
| `engine/service` | engine (C++) | Ingest and Query gRPC server (`--port=0` prints `LISTENING <port>`) | built |
| `server/catalog` | server (Java) | Dataset metadata registry, a library the gateway calls in process | built |
| `server/gateway` | server (Kotlin) | Public gRPC API; validates against the catalog; calls the engine | built |
| `ingest/agent` | ingest (Go) | Reads CSV, streams rows to the engine | built |
| `reports/cli` | reports (Python) | Queries the gateway, prints a table | built |
| `release/` | platform | `analytics_package` target, smoke and package tests | built |
| `testdata/` | platform | Sample CSV and golden output | built |
| `tests/negative/`, `scripts/check_negative.sh` | platform | Targets that must fail to build, and the runner | built |
| `tools/build_defs/tests/analysis/` | platform | 15 `analysistest` checks for macros and the package rule | built |
| `tools/build_defs/legacy/` | platform | Classic-macro counter-example | built |
| `tools/build_defs/service.bzl` | platform | `analytics_grpc_service` | built |

Gazelle is configured in the root `BUILD.bazel`: `map_kind` makes it emit `analytics_go_*` for Go targets.

Each team owns one directory tree, and every target carries a `team:<name>` tag. That lets the M3 work map changed files to owners with `bazel query`.

## Dependency graph

```
reports/cli ─────────► proto:query_py_grpc
server/gateway ──────► server/catalog ──► proto:catalog_java_grpc
       └─────────────► proto:query_java_grpc        (calls engine over gRPC at runtime)
ingest/agent ────────► proto:ingest_go_grpc
engine/service ──────► engine/core
       └─────────────► proto:query_cc_grpc, proto:ingest_cc_grpc
release:analytics ───► engine/service:engine, release:gateway_deploy.jar
                       (java_binary over server/gateway:gateway_lib),
                       ingest/agent/main:agent, reports/cli:cli
```

Expected rebuild behavior. I have not yet checked it with queries; that is M3 work:

- Changing `query.proto` invalidates the services in every language.
- Changing `engine/core` invalidates only `//engine/...` and `//release/...`.

## Visibility rules

| Package | Visible to |
|---|---|
| `//engine/core` | `//engine/...` |
| `//server/catalog` | `//server/...` |
| `//proto/...` | public |
| Binaries | `//release:__pkg__` |

The wrapper macros default to private visibility and never widen it, so teams opt in. The product BUILD files set these rules: `//engine/core` and `//server/catalog` are visible only to their own trees, and the four binaries are visible to `//release:__pkg__` (the gateway also to `//server/...`). Negative cases `vis-engine-core` and `vis-server-catalog` prove that a breach fails the build. Visibility errors surface during analysis and loading, so the negative tests run from a script (`scripts/check_negative.sh`), not as in-graph tests.

## Runtime shape

Five components over one schema. The ingest agent streams CSV rows to the engine (`IngestService.Load`, client-streaming). The report CLI queries the gateway, and the gateway checks the dataset against the catalog before forwarding to the engine (`QueryService.Execute`). Errors map to gRPC statuses: unknown column or aggregation is `INVALID_ARGUMENT`, unknown dataset is `NOT_FOUND`, unreachable engine is `UNAVAILABLE`. The CLI turns any non-OK status into a one-line message and exit code 1. `//release:smoke_test` exercises this path with the engine and gateway on ephemeral ports.

## Layers of policy

| Layer | Where | Examples |
|---|---|---|
| Toolchain and compatibility | `.bazelrc` | `-std=c++20`, Java 17, `remotejdk_17` |
| Per-target policy | `tools/build_defs` macros | warnings as errors, test dependencies, `team` tag, `size = "small"` |
| Dependency versions | `MODULE.bazel` and lock files | one version of each module for the whole repo |

Toolchain flags stay out of macros because they belong in cache keys, and the milestone M2 cache work relies on that.

## Two extension mechanisms

- **Symbolic macros** (`macro()`) wrap existing rules. See [Platform Macros](Platform-Macros).
- **A custom rule with a provider** (`analytics_package`) practices depsets, runfiles, and exec-configured tools. See [Packaging Rule](Packaging-Rule).

A classic macro, `legacy_cc_library` (`tools/build_defs/legacy/`), is kept as a counter-example. It creates a public helper target that any package can depend on. The negative cases `legacy-hdrs-leak` (succeeds) and `modern-helper-private` (fails) show the difference from a symbolic macro, whose internal targets are private.
