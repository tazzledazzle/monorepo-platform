# Roadmap

Seventeen phases in four milestones, tracked in beads (`bd`). Each phase is an epic with a goal and acceptance criteria. Closing a phase requires a comment citing evidence (a gate), which unblocks the next. Status below was read from the tracker on 2026-10-09; run `bd list --label phase:N` or `bd stats` for current numbers. At that time the tracker held 183 issues, 25 closed.

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
| 3 | Proto contracts and composite macros | 2 | In progress |
| 4 | C++ query engine | 3 | Planned |
| 5 | JVM services: catalog and gateway | 3 | Planned |
| 6 | Clients: ingest agent and report CLI | 3 | Planned |
| 7 | Guardrails, release verification and docs | 4, 5, 6 | Planned |
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
| 3 | The `analytics.v1` schema generates stubs for every language through `analytics_proto`, and `analytics_grpc_service` builds services from it. |
| 7 | `analysistest` and negative tests (`scripts/check_negative.sh`) pass; `//release:smoke_test` and `//release:package_test` pass; `docs/architecture.md` and `docs/adding-a-target.md` exist and the clean-build time is recorded; Gazelle generates the macros via `map_kind`. |

The M1 done criteria (from the spec) also require `bazel build //...` and `bazel test //...` to pass on macOS arm64 and `//release:analytics` to produce `analytics.tar`.

## Recorded decisions

- Python uses gRPC stubs (`grpcio` 1.84.0, `protobuf` 7.36.2), not the HTTP/JSON fallback the spec allowed. Closed as `tlow-a6j.10.2`.
- Phase 7 owns the `analysistest` suites, not Phase 2.

## Working a phase

See `docs/planning-in-beads.md` in the repository.
