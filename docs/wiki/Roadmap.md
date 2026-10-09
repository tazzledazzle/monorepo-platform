# Roadmap

Seventeen phases in four milestones, tracked in beads (`bd`). Each phase is an epic with a goal and acceptance criteria. Closing a phase requires a comment citing evidence (a gate), which unblocks the next. Status below was read from the tracker on 2026-10-09; run `bd list --label phase:N` or `bd stats` for current numbers. At that time the tracker held 183 issues, 90 closed. Milestone M1 is built; M2-M4 are not started.

## Milestones

| Milestone | Epic | Scope |
|---|---|---|
| M1 | `tlow-a6j` | Layout, mini analytics product, `tools/build_defs`, shared proto, packaging |
| M2 | `tlow-6ub` | Hermeticity, sanitizers (ASAN/TSAN/UBSAN), local remote cache |
| M3 | `tlow-6p3` | Dependency pinning and conflict detection, affected-target CI, governance |
| M4 | `tlow-yn7` | Platforms and toolchains, cross-compile Linux from macOS, legacy CMake shim |

## Phases

| # | Phase | Needs | Status |
|---|---|---|---|
| 1 | Bootstrap and codegen spike | none | Done |
| 2 | Platform macro and packaging layer | 1 | Done |
| 3 | Proto contracts and composite macros | 2 | Done |
| 4 | C++ query engine | 3 | Done |
| 5 | JVM services: catalog and gateway | 3 | Done |
| 6 | Clients: ingest agent and report CLI | 3 | Done |
| 7 | Guardrails, release verification and docs | 4, 5, 6 | Built; epic open until the clean-build baseline is recorded |
| 8 | Hermetic builds (M2) | 7 | Planned |
| 9 | Sanitizer-clean engine (M2) | 8 | Planned |
| 10 | Local remote cache (M2) | 9 | Planned |
| 11 | Dependency pinning and conflict detection (M3) | 7 | Planned |
| 12 | Affected-target detection (M3) | 7 | Planned |
| 13 | CI pipeline (M3) | 11, 12 | Planned |
| 14 | Governance: lint, ownership, proto compatibility (M3) | 11, 12 | Planned |
| 15 | Platforms and toolchains (M4) | 7 | Planned, no stories yet |
| 16 | Cross-compile Linux from macOS (M4) | 15 | Planned, no stories yet |
| 17 | Legacy CMake strangler shim (M4) | 15 | Planned, no stories yet |

## Gates

| Phase | What must be true to close it |
|---|---|
| 1 | Workspace builds with every dependency pinned; gRPC codegen proven for all five languages. Closed ("Phase 1 verified"). |
| 2 | `tools/build_defs` offers per-language wrappers with shared policy and a reproducible package rule. Closed ("Phase 2 verified"). |
| 3 | The `analytics.v1` schema generates stubs for every language through `analytics_proto`, and `analytics_grpc_service` builds services from it. Closed. |
| 4-6 | The engine, catalog, gateway, agent and CLI build and pass their tests. Closed. |
| 7 | `analysistest` and negative tests (`scripts/check_negative.sh`) pass; `//release:smoke_test` and `//release:package_test` pass; `docs/architecture.md` and `docs/adding-a-target.md` exist and the clean-build time is recorded; Gazelle generates the macros via `map_kind`. All but the recorded build time are done (story `tlow-a6j.15.3`). |

The M1 done criteria (from the spec) also require `bazel build //...` and `bazel test //...` to pass on macOS arm64 and `//release:analytics` to produce `analytics.tar`. Both pass here; `bazel test //...` runs 34 tests.

## What M1 does not show

- It ran on macOS arm64 only. The `gcc` and `msvc` branches of the C++ warning flags have never been compiled.
- The Python CLI in `analytics.tar` cannot run from the unpacked tarball, and the tarball is about 308 MB. See [Packaging Rule](Packaging-Rule).
- The gateway binds all network interfaces and has no `--host` flag. M2 needs it to bind loopback only.
- No CI runs these checks.
- Clean-build baseline: **459 s (7 min 39 s)** for `bazel test //...` from a fresh clone with a new, empty Bazel output base: 8,559 actions (5,108 sandboxed, 65 worker), all 34 tests executed, critical path 60 s. Measured 2026-10-09 on macOS arm64 (Xcode 27) at commit `be4c559`. Bazel's download cache was warm, so a first-ever build on a new machine also pays for fetching dependencies. This is the number M2's remote cache is measured against.

## Recorded decisions

- Python uses gRPC stubs (`grpcio` 1.84.0, `protobuf` 7.36.2), not the HTTP/JSON fallback the spec allowed. Closed as `tlow-a6j.10.2`.
- Phase 7 owns the `analysistest` suites, not Phase 2.
- The gateway jar is `//release:gateway_deploy.jar`, a `java_binary` over the Kotlin gateway library. The spec's `//server/gateway:gateway_deploy.jar` does not exist because `kt_jvm_binary` has no deploy jar.

## Working a phase

See `docs/planning-in-beads.md` in the repository.
