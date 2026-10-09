# Architecture

This page separates what exists from what the design (`docs/superpowers/specs/2026-10-09-m1-monorepo-layout-macros-design.md`) plans. Status labels: **built**, **in progress**, **planned**.

## Layout

| Path | Owner team | Contents | Status |
|---|---|---|---|
| `MODULE.bazel`, `.bazelversion`, `.bazelrc` | platform | Pinned dependencies, Bazel 9.3.0, shared flags | built |
| `go.mod`, `requirements.in`, `maven_install.json` and lock files | platform | Per-ecosystem dependency locks | built |
| `tools/build_defs/` | platform | Wrapper macros, `analytics_package`, shared policy | built |
| `tools/build_defs/proto.bzl`, `proto/analytics/v1/` | platform | `analytics_proto` macro; `common`, `ingest`, `query`, `catalog` `.proto` files (written, build verification pending; see `docs/proto-compatibility.md`) | in progress |
| `engine/core`, `engine/service` | engine (C++) | Columnar table and aggregations; Ingest and Query gRPC server | planned |
| `server/catalog` | server (Java) | Dataset metadata registry | planned |
| `server/gateway` | server (Kotlin) | Public gRPC API; validates against the catalog; calls the engine | planned |
| `ingest/agent` | ingest (Go) | Reads CSV, streams rows to the engine | planned |
| `reports/cli` | reports (Python) | Queries the gateway, prints a table | planned |
| `release/` | platform | `analytics_package` target, smoke and package tests | planned |
| `testdata/`, `tests/negative/` | platform | Sample CSV, golden output, targets that must fail | planned |

Each team owns one directory tree, and every target carries a `team:<name>` tag. That lets the M3 work map changed files to owners with `bazel query`.

## Dependency graph (planned)

```
reports/cli ─────────► proto:query_py_grpc
server/gateway ──────► server/catalog ──► proto:catalog_java_grpc
       └─────────────► proto:query_java_grpc        (calls engine over gRPC at runtime)
ingest/agent ────────► proto:ingest_go_grpc
engine/service ──────► engine/core
       └─────────────► proto:query_cc_grpc, proto:ingest_cc_grpc
release:analytics ───► engine/service, server/gateway:gateway_deploy.jar,
                       ingest/agent, reports/cli
```

Expected rebuild behavior, to be verified in M3 with queries:

- Changing `query.proto` invalidates the services in every language.
- Changing `engine/core` invalidates only `//engine/...` and `//release/...`.

## Visibility rules (planned)

| Package | Visible to |
|---|---|
| `//engine/core` | `//engine/...` |
| `//server/catalog` | `//server/...` |
| `//proto/...` | public |
| Binaries | `//release:__pkg__` |

The wrapper macros already default to private visibility and never widen it, so teams opt in. Visibility errors surface while BUILD files load, so the planned negative tests run from a script (`scripts/check_negative.sh`), not as in-graph tests.

## Runtime shape (planned)

Five components over one schema. The ingest agent streams CSV rows to the engine (`IngestService.Load`, client-streaming). The report CLI queries the gateway, and the gateway checks the dataset against the catalog before forwarding to the engine (`QueryService.Execute`). Errors map to gRPC statuses: unknown column or aggregation is `INVALID_ARGUMENT`, unknown dataset is `NOT_FOUND`, unreachable engine is `UNAVAILABLE`. The CLI turns any non-OK status into a one-line message and exit code 1.

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

The design also plans a classic macro, `legacy_cc_library`, as a counter-example. It would show that a classic macro lets outside targets depend on its helper target, which a symbolic macro forbids. It is not built (Phase 7).
