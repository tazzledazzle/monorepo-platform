# Architecture

One Bazel workspace builds a small analytics product in four languages. Teams own packages;
the platform team owns `//tools/build_defs`, the only place product BUILD files load from.

Status key: **exists** is in the tree and tested. **Planned** is not built yet.

## Layout

| Path | Team | Language | Role | Status |
|---|---|---|---|---|
| `proto/analytics/v1/` | platform | proto | Shared gRPC contract: `common`, `ingest`, `query`, `catalog` | exists |
| `engine/core/` | engine | C++ | In-memory columnar table and aggregations; no proto, no gRPC | exists |
| `engine/service/` | engine | C++ | gRPC server over `engine/core` (`IngestService`, `QueryService`) | exists |
| `server/catalog/` | server | Java | Dataset-name to schema registry | exists |
| `server/gateway/` | server | Kotlin | Public `QueryService`; checks the catalog, forwards to the engine | exists |
| `ingest/agent/` | ingest | Go | Reads CSV, streams rows to the engine | exists |
| `reports/cli/` | reports | Python | Queries the gateway, prints a table | exists |
| `tools/build_defs/` | platform | Starlark | `analytics_*` macros, `analytics_package` rule, their tests | exists |
| `tests/negative/`, `scripts/check_negative.sh` | platform | shell | Targets that must fail to build, with the expected error | exists |
| `release/` | platform | Starlark | `analytics_package` target that ships all binaries | in progress (Phase 7) |
| Build-time and rebuild queries, load-source lint | platform | | M3 | planned |

The design is in `docs/superpowers/specs/2026-10-09-m1-monorepo-layout-macros-design.md`.

## Dependency graph

Compile-time edges, taken from the BUILD files:

```
reports/cli ─────────► proto:query_py_grpc
server/gateway ──────► server/catalog ──► proto:catalog_java_grpc, proto:common_java_grpc
       └─────────────► proto:query_java_grpc
ingest/agent ────────► proto:ingest_go_grpc, proto:common_go_grpc
engine/service ──────► engine/core
       └─────────────► proto:ingest_cc_grpc, proto:query_cc_grpc
```

Runtime edges are gRPC calls, not Bazel dependencies:
`ingest/agent` → `engine/service` ← `server/gateway` ← `reports/cli`.

Each `.proto` becomes one target per language (`<name>_cc_grpc`, `_java_grpc`, `_go_grpc`,
`_py_grpc`). A consumer depends on its own language's target, so nobody builds stubs they do not use.

## Visibility

Targets are private unless the BUILD file opens them. The macros never widen visibility.

| Target | Visible to | Why |
|---|---|---|
| `//proto/...` | public | The contract is shared by every team. |
| `//engine/core` | `//engine:__subpackages__` | Internal to the engine team. |
| `//engine/service` | `//engine:__subpackages__`, `//release:__pkg__` | Release ships the binary. |
| `//server/catalog` | `//server:__subpackages__` | Internal to the server team. |
| `//server/gateway` | `//server:__subpackages__`, `//release:__pkg__` | |
| `//ingest/agent` (library) | `//ingest:__subpackages__` | |
| `//ingest/agent/main:agent` | public | A binary. |
| `//reports/cli:report_lib` | `//reports:__subpackages__` | |
| `//reports/cli:cli` | public | A binary. |

`scripts/check_negative.sh` proves two of these boundaries hold: a breach into `//engine/core`
and into `//server/catalog` must fail to build. It also proves that `-Werror` fires in C++,
Java, and Kotlin.

## How the pieces connect

1. A team's BUILD file loads an `analytics_*` macro from `//tools/build_defs`.
2. The macro wraps the language's real rule and adds per-target policy: a `team:<name>` tag,
   warnings as errors, test defaults (`size = "small"`, test dependencies, pytest shim).
3. `analytics_proto` generates all four language stubs from one `.proto`.
4. `analytics_grpc_service` builds a library, a binary, and a test for a C++ or Kotlin gRPC server
   from `protos = [...]`, adding the gRPC runtime.
5. `.bazelrc` holds repo-wide toolchain flags (language levels, test output). Per-target policy
   stays in the macros. See [adding-a-target.md](adding-a-target.md).
6. Gazelle generates Go BUILD files that call the Go macros.

Macro sources: [common.bzl](../tools/build_defs/common.bzl),
[cc.bzl](../tools/build_defs/cc.bzl), [java.bzl](../tools/build_defs/java.bzl),
[kt.bzl](../tools/build_defs/kt.bzl), [go.bzl](../tools/build_defs/go.bzl),
[py.bzl](../tools/build_defs/py.bzl), [proto.bzl](../tools/build_defs/proto.bzl),
[service.bzl](../tools/build_defs/service.bzl), [package.bzl](../tools/build_defs/package.bzl).

## Expected rebuild behavior

Stated in the design; M3 will verify it with queries. Not yet measured.

- Changing `query.proto` invalidates the services in every language that depends on it.
- Changing `engine/core` invalidates only `//engine/...` and `//release/...`.

## Clean-build time

**Clean-build baseline.** **459 s (7 min 39 s)** for `bazel test //...` from a fresh clone with a new, empty Bazel output base: 8,559 actions (5,108 sandboxed, 65 worker), all 34 tests executed, critical path 60 s. Measured 2026-10-09 on macOS arm64 (Xcode 27) at commit `be4c559`. Bazel's download cache was warm, so a first-ever build on a new machine also pays for fetching dependencies. This is the number M2's remote cache is measured against.
