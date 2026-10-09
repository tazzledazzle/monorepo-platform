# M1 Design: Monorepo Layout and Platform Macros

- **Date:** 2026-10-09
- **Status:** Approved design, pending implementation plan
- **Series:** Unified Bazel build platform, milestone 1 of 4

## Context

This repository is a learning sandbox. It recreates, at small scale, a unified Bazel build platform like Tableau's "Blitz" monorepo: several languages and teams, one build graph, shared rules, pinned dependencies, and incremental CI.

The work is split into four milestones. Each gets its own spec, plan, and working result:

| Milestone | Scope | Depends on |
|---|---|---|
| **M1** (this spec) | Directory layout, mini analytics product, `tools/build_defs` macro layer, shared proto, packaging rule | none |
| M2 | Hermeticity, sanitizer configs (ASAN/TSAN/UBSAN), local remote cache | M1 |
| M3 | Dependency pinning and conflict detection (UC-04), `bazel query` affected-target CI (UC-02) | M1, benefits from M2 |
| M4 | Platforms and toolchains, cross-compiling Linux from macOS, legacy CMake shim | M1 to M3 |

M1 must not prevent any later milestone. The section "Groundwork for later milestones" lists what that requires.

### Starting state

- Bazel 9.3.0 with Bzlmod (`MODULE.bazel`, no `WORKSPACE`), run via bazelisk.
- Working `cpp/`, `java/`, and `go/` greeter packages that already solved three Bazel 9 problems:
  - explicit `load()` statements, because the native rules were removed
  - JUnit via `rules_jvm_external`
  - the `apple_support` toolchain, which fixes an `SDKSettings.json` error on Xcode 27
- Host: macOS arm64, Xcode 27, Apple clang 21.

## Goals

1. A five-language product (C++, Java, Kotlin, Go, Python) whose build graph has real depth and is shared across teams.
2. A platform layer in `tools/build_defs` that product teams use instead of the raw `rules_*` rules. It covers both ways of extending Bazel: symbolic macros and a custom rule with a provider.
3. Guardrails that are demonstrably enforced: `-Werror` and visibility boundaries, proven by negative tests.

## Non-goals (M1)

- Remote cache, sanitizers, and `.bazelrc` configs beyond the basics (M2).
- CI pipelines, affected-target detection, lint enforcement of `load()` sources (M3).
- Windows/Linux toolchains and cross-compiling (M4).
- Product features beyond what the smoke test needs.

## Directory layout

```
.
├── MODULE.bazel             # every external dependency, pinned
├── MODULE.bazel.lock
├── .bazelversion            # 9.3.0
├── .bazelrc                 # shared flags, ends with try-import %workspace%/user.bazelrc
├── go.mod / go.sum
├── requirements.in / requirements_lock.txt
├── maven_install.json
├── proto/analytics/v1/      # common.proto, query.proto, ingest.proto, catalog.proto
├── engine/                  # team:engine (C++), "desktop" analog
│   ├── core/                #   columnar table + aggregations; no gRPC
│   └── service/             #   QueryService + IngestService gRPC server (cc_binary)
├── server/                  # team:server (JVM)
│   ├── catalog/             #   Java: dataset metadata registry
│   └── gateway/             #   Kotlin: public gRPC API; uses catalog; calls engine
├── ingest/agent/            # team:ingest (Go): reads CSV, streams rows to engine
├── reports/cli/             # team:reports (Python): queries gateway, prints a table
├── release/                 # analytics_package target + release tests
├── testdata/                # sales.csv, golden report output
├── tests/negative/          # targets that must fail to build (tagged manual)
├── scripts/check_negative.sh
├── tools/build_defs/
│   ├── cc.bzl java.bzl kt.bzl go.bzl py.bzl
│   ├── proto.bzl            # analytics_proto
│   ├── service.bzl          # analytics_grpc_service
│   ├── package.bzl          # analytics_package rule + AnalyticsPackageInfo
│   ├── packaging/           # make_tarball.py (exec tool for analytics_package)
│   ├── tests/               # analysistest suites for the macros and the rule
│   └── legacy/              # one classic macro kept as a counter-example
└── docs/
    ├── architecture.md
    └── adding-a-target.md
```

The greeter packages `cpp/`, `java/`, and `go/` are deleted. Their fixes carry over into `MODULE.bazel` and the macros.

## Dependency graph

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

### Visibility

| Package | Visible to |
|---|---|
| `//engine/core` | `//engine/...` |
| `//server/catalog` | `//server/...` |
| `//proto/...` | public |
| Binaries | `//release:__pkg__` |

Expected rebuild behavior, which M3 will verify with queries:

- Changing `query.proto` invalidates the services in every language.
- Changing `engine/core` invalidates only `//engine/...` and `//release/...`.

## Product behavior

| Component | Behavior |
|---|---|
| `engine/core` | In-memory columnar table. Aggregations: `sum`, `avg`, `count`, `min`, `max`, with optional single-column `group_by`. |
| `engine/service` | `IngestService.Load` (client-streaming rows into a named dataset) and `QueryService.Execute`. Flag `--port`; `--port=0` binds a free port and prints `LISTENING <port>` on stdout. |
| `server/catalog` | Registry mapping dataset name to schema (column names and types). Seeded in memory. |
| `server/gateway` | `QueryService.Execute`: validates the dataset against the catalog, then forwards to the engine. Flags `--port` (same port-0 behavior) and `--engine=<host:port>`. |
| `ingest/agent` | `agent --engine=<host:port> --dataset=<name> <file.csv>` |
| `reports/cli` | `cli --gateway=<host:port> --dataset=<name> --agg=sum:revenue --group-by=region`. Prints a table using `tabulate` (pip). |

### Error handling

| Where | Condition | gRPC status |
|---|---|---|
| Engine | Unknown column or invalid aggregation | `INVALID_ARGUMENT` |
| Gateway | Unknown dataset | `NOT_FOUND` |
| Gateway | Engine unreachable | `UNAVAILABLE` |

The CLI maps any non-OK status to a one-line stderr message and exit code 1.

## Platform layer: `tools/build_defs`

**Rule for product teams:** product BUILD files load only from `//tools/build_defs`. M1 states this rule in the docs; M3 enforces it with a lint check. For Go, Gazelle is set up with `# gazelle:map_kind` so it generates our macros.

### Per-language symbolic macros

Each wrapper is defined with `macro(inherit_attrs = <wrapped rule>, implementation = ...)`.

| Macro | Wraps | Defaults it adds |
|---|---|---|
| `analytics_cc_library`, `analytics_cc_binary`, `analytics_cc_test` | rules_cc | Warning flags chosen by `select()` on `@rules_cc//cc/compiler:{clang,gcc,msvc-cl}`. clang/gcc get `-Wall -Wextra -Werror`; msvc-cl gets `/W4 /WX`. Tests also get `@googletest//:gtest_main`. |
| `analytics_java_library`, `analytics_java_binary`, `analytics_java_test` | rules_java | `javacopts = ["-Xlint:all", "-Werror"]`. Tests get JUnit and a `test_class` derived from the package and target name. |
| `analytics_kt_library`, `analytics_kt_binary`, `analytics_kt_test` | rules_kotlin | Warnings as errors (a `kt_kotlinc_options` target). Tests get JUnit. |
| `analytics_go_library`, `analytics_go_binary`, `analytics_go_test` | rules_go | Team tag. Generated by Gazelle via `map_kind`. |
| `analytics_py_library`, `analytics_py_binary`, `analytics_py_test` | rules_python | Tests run under pytest via a shared entry-point shim. |

**Common to all wrappers:**

- Tests default to `size = "small"`.
- A required `team` attribute adds a `team:<name>` tag.
- Visibility defaults to private.

**Policy split:** repo-wide flags that affect the toolchain or compatibility, such as `--cxxopt=-std=c++20` and the Java language level, live in `.bazelrc`, never in macros. Per-target policy, such as warnings, test dependencies, and tags, lives in macros.

### `analytics_grpc_service` (composite macro)

```python
analytics_grpc_service(
    name = "gateway",
    lang = "kt",          # attr.string(values = ["cc", "kt"], configurable = False)
    team = "server",
    srcs = glob(["src/main/kotlin/**/*.kt"]),
    test_srcs = glob(["src/test/kotlin/**/*.kt"]),
    protos = ["//proto/analytics/v1:query"],
    main = "analytics.gateway.MainKt",   # kt only
    deps = ["//server/catalog"],
)
# Expands to :gateway_lib, :gateway (binary), :gateway_test
```

- Each entry in `protos` resolves to the matching per-language target. For `cc`, the macro appends `_cc_grpc`; for `kt`, `_java_grpc`.
- `lang` must be non-configurable, because the macro branches on it while the BUILD file is being evaluated.
- Only `cc` and `kt` are supported, because those are the only servers. Adding another language is the UC-03 exercise.

### `analytics_proto` (macro)

`analytics_proto(name, srcs, deps)` expands to:

| Target | Rules | From |
|---|---|---|
| `<name>_proto` | `proto_library` | `@protobuf//bazel` |
| `<name>_cc_grpc` | `cc_proto_library` + `cc_grpc_library` | `@grpc` |
| `<name>_java_grpc` | `java_proto_library` + `java_grpc_library` | `@grpc-java` |
| `<name>_go_grpc` | `go_proto_library` with the gRPC v2 compiler | `@rules_go` |
| `<name>_py_grpc` | `py_proto_library` + `py_grpc_library` | `@grpc` python_rules |

Consumers depend on the target for their language. A language's generated code is built only if something depends on it.

### `analytics_package` (custom rule)

```python
analytics_package(
    name = "analytics",
    components = {        # attr.label_keyed_string_dict: label → install subdirectory
        "//engine/service": "engine",
        "//server/gateway:gateway_deploy.jar": "gateway",
        "//ingest/agent": "agent",
        "//reports/cli": "cli",
    },
    deps = [],            # other analytics_package targets to nest
)
```

**Implementation:**

1. For each component, it collects the files from `DefaultInfo` plus the component's runfiles. Each file is placed at `<subdir>/<path>` in a depset.
2. It merges the `AnalyticsPackageInfo` providers from `deps`.
3. It runs one action with `//tools/build_defs/packaging:make_tarball`, a `py_binary` attached with `cfg = "exec"`. The action writes:
   - `<name>.tar`, with sorted entries, zeroed mtimes, and fixed uid/gid, so the output is reproducible
   - `<name>.MANIFEST.json`, listing each file's path, sha256, and source label

**Provider:** `AnalyticsPackageInfo(files_by_path = depset)`, which is what makes nesting possible.

**Duplicate paths:** if two entries map to the same install path, the action fails and names both source labels.

**Known limitation:** Python binaries are packaged together with their runfiles tree. This is documented, not hidden.

**Why it's hand-written:** `rules_pkg` is the production choice. This rule exists so the sandbox teaches providers, depsets, runfiles, and exec-configured tools.

### Legacy counter-example

`tools/build_defs/legacy/legacy_cc_library.bzl` is a classic macro, `def legacy_cc_library(name, **kwargs)`. It calls `cc_library` and also creates a public `<name>_hdrs` filegroup. A negative test shows that a target outside the package can depend on that helper. The symbolic version doesn't allow this, because targets created inside a symbolic macro are private to it by default.

## External dependencies

All pinned in `MODULE.bazel`. Exact versions are chosen in the spike task and recorded there.

| Module | Purpose | Lock file |
|---|---|---|
| `apple_support` (declared before `rules_cc`) | Xcode-aware C++ toolchain | `MODULE.bazel.lock` |
| `rules_cc`, `rules_java`, `rules_kotlin`, `rules_python`, `rules_go`, `gazelle`, `rules_shell` | Language rules | `MODULE.bazel.lock` |
| `bazel_skylib` | `analysistest` | `MODULE.bazel.lock` |
| `protobuf`, `grpc`, `grpc-java` | Proto and gRPC code generation | `MODULE.bazel.lock` |
| `googletest` | C++ tests | `MODULE.bazel.lock` |
| `rules_jvm_external` (`maven.install`) | JUnit, grpc-java runtime, Kotlin coroutines if needed | `maven_install.json` |
| `rules_python` `pip.parse` | `tabulate`, `pytest`, `grpcio` runtime | `requirements_lock.txt` |
| `gazelle` `go_deps` | `google.golang.org/grpc`, `google.golang.org/protobuf` | `go.sum` |

## Testing

| Layer | Target(s) | Checks |
|---|---|---|
| Unit | `*_test` next to every library | Product logic: aggregations, catalog lookups, CSV parsing, CLI formatting |
| Platform | `//tools/build_defs/tests:all` (Skylib `analysistest`) | The cc macro adds the warning flags. The test macros inject gtest/JUnit. `analytics_grpc_service` creates `_lib`, binary, and `_test`. `analytics_package`'s provider has the expected paths, and duplicate paths fail. |
| Negative | `tests/negative/*` (tagged `manual`) run by `scripts/check_negative.sh` | Each case declares its expected outcome. Must fail, with the error containing the expected text: (a) visibility breach into `//engine/core`; (b) `-Werror` violation (unused variable). Must succeed, which demonstrates the leak: (c) dependency on the legacy `_hdrs` helper. |
| End-to-end | `//release:smoke_test` (`sh_test`, `size = "medium"`) | Starts the engine on port 0 and the gateway on port 0 against it. Ingests `testdata/sales.csv` with the agent, runs the CLI, and diffs stdout against `testdata/sales_by_region.golden`. Kills both processes on exit (trap). |
| Package | `//release:package_test` | Unpacks `analytics.tar` and checks every `MANIFEST.json` sha256. Checks the expected executables are present. |

Visibility errors happen while BUILD files load, before `analysistest` runs, which is why negative cases need an external script rather than an in-graph test.

## Groundwork for later milestones

- **M2:**
  - `.bazelrc` is split into `common`/`build`/`test` sections and ends with `try-import %workspace%/user.bazelrc`.
  - Nothing reads `$HOME` or uses absolute host paths.
  - Only the smoke test uses the network, and only on localhost.
  - The package tarball is reproducible, so remote-cache hits can be compared across runs.
- **M3:**
  - Every target carries a `team:` tag, and each team has one directory tree.
  - All four lock files are committed.
- **M4:**
  - C++ flags are chosen by compiler via `select()`.
  - No macro or BUILD file hard-codes macOS paths or flags.

## Risks

| Risk | Mitigation |
|---|---|
| gRPC code generation (especially C++ and Python) is incompatible with Bazel 9 + Bzlmod + Xcode 27 | The first plan task is a spike: build `//proto/...` for all five languages before any product code. Fallback for Python: plain protobuf messages over HTTP/JSON to a gateway endpoint. |
| C++ gRPC cold build takes many minutes | Accepted. The measured clean-build time becomes M2's remote-cache baseline. |
| Symbolic-macro edge cases (`inherit_attrs` with `select()`, rule-from-`.bzl` inheritance) | The analysistest suite covers each macro. If a wrapper can't inherit cleanly, list its attributes explicitly and note why in `adding-a-target.md`. |
| Kotlin `-Werror` conflicts with generated or interop code | Applies only to first-party sources; generated gRPC stubs are Java. |

## Done criteria

1. `bazel build //...` and `bazel test //...` pass on macOS arm64.
2. `bazel build //release:analytics` produces `analytics.tar` and `analytics.MANIFEST.json`, and `//release:smoke_test` and `//release:package_test` pass.
3. `scripts/check_negative.sh` exits 0.
4. `docs/architecture.md` (layout, graph, visibility) and `docs/adding-a-target.md` (per-language macro usage, policy split, Gazelle workflow) exist.
5. The clean-build time in a fresh clone is recorded in `docs/architecture.md`. It's measured, not required to hit a target.

## As built: deviations from this spec

Recorded at M1 completion (2026-10-09). The spec above is the design; these are the differences in the code.

- **`inherit_attrs`.** Only `kt_jvm_*`, `go_library` and `go_test` are real rules and inherit their attributes. rules_cc, rules_java, rules_python and `go_binary` export wrapper functions, so those macros list attributes explicitly (the fallback this spec anticipated).
- **`team` enforcement.** `team` is not `mandatory = True`. On Bazel 9.3.0 a missing mandatory attribute of a symbolic macro prints an error but `bazel build` still exits 0, so `team_tags` in `common.bzl` checks it with `fail()`.
- **Gateway package component.** `//server/gateway:gateway_deploy.jar` does not exist because `kt_jvm_binary` has no deploy jar. `//release:gateway_deploy.jar` is a `java_binary` over the gateway library.
- **`analytics_proto`.** Takes `strip_import_prefix` and `importpath`. `<name>_java_grpc` is a `java_library` that re-exports the stubs, messages and dep protos (javac strict-deps). Go targets use both the `go_proto` and `go_grpc_v2` compilers. C++ stubs are built with `generate_cc` directly.
- **Python stubs.** `.bazelrc` sets `--incompatible_default_to_explicit_init_py` so generated stubs merge as a namespace package.
- **Ingest semantics.** A repeated `Load` appends and requires an identical schema (per `docs/proto-compatibility.md`). An unknown dataset on `Execute` returns `NOT_FOUND`.
- **Packaging.** The same file reached twice at one install path is one entry; only different files at one path fail. The packaged Python CLI cannot run from the unpacked tarball (runfiles).
- **Wire format.** Cell values are typed column vectors, with no per-cell `Value` message and no NULLs in v1.
- **Not verified.** The gcc and msvc warning branches of the cc macros; any platform other than macOS arm64.
