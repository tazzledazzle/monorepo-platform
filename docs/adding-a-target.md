# Adding a target

Product BUILD files load only from `//tools/build_defs`. Never load `@rules_go`, `@rules_cc`
and the like directly: the `analytics_*` macros add the team tag, warnings as errors, and test
defaults. M3 will enforce this with a lint; until then it is a review rule.
For the layout and dependency graph, see [architecture.md](architecture.md).

## Choose the macro

| Language | Library | Binary | Test | Defined in |
|---|---|---|---|---|
| C++ | `analytics_cc_library` | `analytics_cc_binary` | `analytics_cc_test` | [cc.bzl](../tools/build_defs/cc.bzl) |
| Java | `analytics_java_library` | `analytics_java_binary` | `analytics_java_test` | [java.bzl](../tools/build_defs/java.bzl) |
| Kotlin | `analytics_kt_library` | `analytics_kt_binary` | `analytics_kt_test` | [kt.bzl](../tools/build_defs/kt.bzl) |
| Go | `analytics_go_library` | `analytics_go_binary` | `analytics_go_test` | [go.bzl](../tools/build_defs/go.bzl) |
| Python | `analytics_py_library` | `analytics_py_binary` | `analytics_py_test` | [py.bzl](../tools/build_defs/py.bzl) |
| Proto | `analytics_proto` | | | [proto.bzl](../tools/build_defs/proto.bzl) |
| gRPC server (C++ or Kotlin) | `analytics_grpc_service` | | | [service.bzl](../tools/build_defs/service.bzl) |

Every macro requires `team`, which becomes the tag `team:<name>`. Visibility is private unless you
set it. Tests default to `size = "small"`.

## Per-language examples

Copied from the repo. Replace names, sources, and `team`.

### C++ (`engine/core/BUILD.bazel`)

```python
load("//tools/build_defs:cc.bzl", "analytics_cc_library", "analytics_cc_test")

analytics_cc_library(
    name = "core",
    srcs = [
        "aggregate.cc",
        "table.cc",
    ],
    hdrs = [
        "aggregate.h",
        "status.h",
        "table.h",
    ],
    team = "engine",
    visibility = ["//engine:__subpackages__"],
)

analytics_cc_test(
    name = "table_test",
    srcs = ["table_test.cc"],
    team = "engine",
    deps = [":core"],
)
```

The macro adds `-Wall -Wextra -Werror` and gtest. Do not list gtest in `deps`.

### Java (`server/catalog/BUILD.bazel`)

```python
load("//tools/build_defs:java.bzl", "analytics_java_library", "analytics_java_test")

analytics_java_library(
    name = "catalog",
    srcs = ["src/main/java/analytics/catalog/Catalog.java"],
    team = "server",
    visibility = ["//server:__subpackages__"],
    deps = [
        "//proto/analytics/v1:catalog_java_grpc",
        "//proto/analytics/v1:common_java_grpc",
    ],
)
```

The macro adds `-Xlint:all -Werror`. Tests get JUnit and a derived `test_class`.

### Kotlin (`tools/build_defs/tests/samples/kt/BUILD.bazel`)

```python
load("//tools/build_defs:kt.bzl", "analytics_kt_library", "analytics_kt_test")

analytics_kt_library(
    name = "lib",
    srcs = ["src/main/kotlin/sample/Greeter.kt"],
    team = "platform",
)

analytics_kt_test(
    name = "GreeterTest",
    srcs = ["src/test/kotlin/sample/GreeterTest.kt"],
    team = "platform",
    deps = [":lib"],
)
```

Kotlin warnings are errors. A team cannot override `kotlinc_opts`.

### Python (`reports/cli/BUILD.bazel`)

```python
load("//tools/build_defs:py.bzl", "analytics_py_binary", "analytics_py_library", "analytics_py_test")

analytics_py_library(
    name = "report_lib",
    srcs = ["report_lib.py"],
    imports = ["../.."],
    team = "reports",
    visibility = ["//reports:__subpackages__"],
    deps = [
        "//proto/analytics/v1:query_py_grpc",
        "@pip//grpcio",
        "@pip//protobuf",
        "@pip//tabulate",
    ],
)

analytics_py_binary(
    name = "cli",
    srcs = ["main.py"],
    main = "main.py",
    team = "reports",
    visibility = ["//visibility:public"],
    deps = [":report_lib"],
)
```

Tests run under pytest through a shared shim; do not write a `main`. Python consumers of a
proto depend on `@pip//protobuf` next to `@pip//grpcio`.

### Go (`ingest/agent/BUILD.bazel`)

```python
load("//tools/build_defs:go.bzl", "analytics_go_library", "analytics_go_test")

analytics_go_library(
    name = "agent",
    srcs = [
        "client.go",
        "csvdata.go",
    ],
    importpath = "example.com/three_languages_one_workspace/ingest/agent",
    team = "ingest",
    visibility = ["//ingest:__subpackages__"],
    deps = [
        "//proto/analytics/v1:common_go_grpc",
        "//proto/analytics/v1:ingest_go_grpc",
        "@org_golang_google_grpc//:grpc",
    ],
)
```

Gazelle writes Go BUILD files; see [Gazelle workflow](#gazelle-workflow-go).

## Protos: `analytics_proto`

One `.proto` yields `<name>_proto` plus `_cc_grpc`, `_java_grpc`, `_go_grpc`, and `_py_grpc`.
From `proto/analytics/v1/BUILD.bazel`:

```python
load("//tools/build_defs:proto.bzl", "analytics_proto")

_GO = "example.com/three_languages_one_workspace/proto/analytics/v1/"

analytics_proto(
    name = "ingest",
    srcs = ["ingest.proto"],
    importpath = _GO + "ingestpb",
    strip_import_prefix = "/proto",
    team = "platform",
    visibility = ["//visibility:public"],
    deps = [":common_proto"],
)
```

Depend on the stub for your language, for example `//proto/analytics/v1:ingest_go_grpc`.
`deps` lists other `<x>_proto` targets. The stubs of unused languages are never built.
If you add a proto that Go code imports, add a `# gazelle:resolve go` line to the root
`BUILD.bazel` (see below).

## gRPC servers: `analytics_grpc_service`

For a C++ or Kotlin server, one call yields `:<name>_lib`, `:<name>` (binary), and `:<name>_test`.
From `engine/service/BUILD.bazel`:

```python
load("//tools/build_defs:service.bzl", "analytics_grpc_service")

analytics_grpc_service(
    name = "engine",
    srcs = [
        "dataset_store.cc",
        "dataset_store.h",
        "engine_service.cc",
        "engine_service.h",
        "main.cc",  # dropped from the library by the macro, compiled into the binary
    ],
    lang = "cc",
    main = "main.cc",
    protos = [
        "//proto/analytics/v1:ingest",
        "//proto/analytics/v1:query",
    ],
    team = "engine",
    test_srcs = ["engine_service_test.cc"],
    visibility = [
        "//engine:__subpackages__",
        "//release:__pkg__",
    ],
    deps = ["//engine/core"],
)
```

`protos` takes the bare proto name (`:query`, not `:query_cc_grpc`); the macro picks the stub for
`lang` and adds the gRPC runtime. For `lang = "kt"`, `main` is the main class, as in
`server/gateway/BUILD.bazel` (`main = "analytics.gateway.MainKt"`).

## Policy split

| Lives in | What | Why |
|---|---|---|
| `.bazelrc` | Repo-wide toolchain flags: C++20, Java 17, Python `__init__.py` handling, `--test_output=errors` | They affect cache keys for the whole toolchain. |
| Macros in `tools/build_defs` | Per-target policy: warnings as errors, test dependencies, `size = "small"`, `team` tag | Each target gets it, and the platform team changes it in one place. |

Put a new flag in `.bazelrc` if it changes how the toolchain behaves everywhere. Put it in a
macro if it is a rule about individual targets.

## Gazelle workflow (Go)

The root `BUILD.bazel` configures Gazelle with:

```python
# gazelle:prefix example.com/three_languages_one_workspace
# gazelle:exclude bazel-*
# gazelle:map_kind go_library analytics_go_library //tools/build_defs:go.bzl
# gazelle:map_kind go_binary analytics_go_binary //tools/build_defs:go.bzl
# gazelle:map_kind go_test analytics_go_test //tools/build_defs:go.bzl
```

It also excludes `tools` and `tests` (hand-written fixtures), turns off proto generation
(`# gazelle:proto disable_global`; `analytics_proto` owns that), and maps the Go proto import
paths to our `*_go_grpc` targets with `# gazelle:resolve go ...`.

To add a Go package:

1. Write the `.go` files.
2. Run `bazel run //:gazelle`. It creates `BUILD.bazel` with `analytics_go_library`,
   `analytics_go_binary`, and `analytics_go_test`, and fills `srcs`, `deps`, `importpath`.
3. **Add `team = "<name>"` to each new rule.** Gazelle cannot invent it. Gazelle keeps
   attributes it does not manage, so later runs preserve `team`, `visibility`, and `data`.
4. Set `visibility` if other packages need the target.
5. Run `bazel run //:gazelle` again. It must produce no diff.

Gazelle also reorders attributes in the other packages' BUILD files (buildifier order). That is
expected and harmless.

A rule without `team` is an error at load time, for `bazel build`, `bazel test`, and `bazel query`
alike:
`missing value for mandatory attribute 'team' (package //ingest/agent): every analytics_* target must name its owning team`.

`team` is not declared `mandatory = True`. On Bazel 9.3.0 a missing mandatory attribute of a symbolic
macro prints an error but `bazel build` still exits 0; only `bazel query` fails. `team_tags` in
`tools/build_defs/common.bzl` checks it with `fail()` instead. `scripts/check_negative.sh` covers
both the query and the build case.

## Macro internals

- `go_library`, `go_test`, and the Kotlin rules are real rules, so their macros use
  `inherit_attrs` and accept every attribute of the wrapped rule.
- `cc_*`, `java_*`, `py_*`, and `go_binary` are wrapper functions, which `inherit_attrs` cannot
  take. Those macros list their attributes explicitly (for example `_GO_BINARY_ATTRS` in
  `go.bzl`). If a macro rejects an attribute the wrapped rule supports, add it to that list.

## Running tests

```
bazel test //engine/... //server/... //reports/... //ingest/...   # product tests
bazel test //tools/build_defs/...                                 # macro and rule tests
scripts/check_negative.sh                                         # boundaries and -Werror must fail
bazel run //:gazelle                                              # regenerate Go BUILD files
```

## Packaging with `analytics_package`

```python
load("//tools/build_defs:package.bzl", "analytics_package")

analytics_package(
    name = "analytics",
    components = {"//engine/service": "engine"},  # label -> install subdirectory
    deps = [],                                    # nest other analytics_package targets
)
```

Outputs `<name>.tar` and `<name>.MANIFEST.json` (path, sha256, source label per file).
Both are byte-reproducible; check with `tools/build_defs/packaging/check_reproducible.sh`.
Two different files mapping to the same install path fail the build, naming both source labels.

### Known limitation: Python binaries ship with their runfiles tree

For each component, the rule packages `DefaultInfo` files plus default runfiles, each at
`<subdir>/<short_path>`. A `py_binary` therefore carries its whole runfiles file set
(interpreter, sources, pip packages). Runfiles symlinks, tree artifacts, and dangling
symlink artifacts (such as rules_python's `_x.venv/bin/python3`, skipped with a warning)
are not packaged. This is documented, not hidden: `rules_pkg` is the production choice.
