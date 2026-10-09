# Findings and Gotchas

Problems I hit while building this, each with its fix. Versions are those pinned in `MODULE.bazel`: Bazel 9.3.0, protobuf 36.2, grpc and grpc-java 1.84.0, rules_go 0.64.2, rules_python 2.4.1. The gRPC codegen findings come from the spike, issue `tlow-a6j.10.1`; the rest came while building phases 3-7. All were seen on macOS arm64 with Xcode 27.

## Bazel 9 and toolchains

### Native language rules are gone

**Symptom:** `cc_library` and `java_library` are undefined in BUILD files and macros.
**Fix:** `load()` them explicitly, for example `load("@rules_cc//cc:cc_library.bzl", "cc_library")`. Every file in `tools/build_defs` does this.

### Xcode 27 and `SDKSettings.json`

**Symptom:** C++ builds fail on macOS with an error about `SDKSettings.json`.
**Fix:** declare `apple_support` before `rules_cc` in `MODULE.bazel`. Its toolchain knows the SDK files that newer Apple clang reports.

### Toolchain flags versus per-target policy

**Decision, not bug:** language levels (`-std=c++20`, Java 17) go in `.bazelrc` because they affect the toolchain and cache keys. Warnings, test dependencies, and tags go in macros. See [Architecture](Architecture).

## Symbolic macros

### `inherit_attrs` cannot take plain functions

**Symptom:** `macro(inherit_attrs = cc_library)` fails.
**Cause:** `inherit_attrs` accepts a rule or macro symbol. `rules_cc`, `rules_java`, and `rules_python` export their rules as wrapper functions, and so does `rules_go`'s `go_binary`.
**Fix:** use `inherit_attrs = "common"` and list the language attributes explicitly. `kt_jvm_*`, `go_library`, and `go_test` are real rules and inherit fully. Details: [Platform Macros](Platform-Macros).

### A macro cannot read a configurable `srcs`

**Symptom:** the macro needs to look at `srcs` (to derive a JUnit `test_class`, or to build pytest arguments) but a `select()` could be there.
**Fix:** declare `srcs` with `configurable = False` on those macros. `team` and `size` are non-configurable for the same reason.

### Macros must forward `visibility`

The spike hit a visibility error on generated Python targets. Pass `visibility` to every target a macro creates. Targets created inside a symbolic macro are private to it unless you do.

### A missing mandatory macro attribute does not fail the build

**Symptom:** a wrapper called without `team` prints `missing value for mandatory attribute 'team'` as an ERROR, yet `bazel build` and `bazel test` exit 0. Only `bazel query` fails.
**Cause:** on Bazel 9.3.0, a missing mandatory attribute of a symbolic macro is reported but does not stop the build commands.
**Fix:** `team` is not declared `mandatory`. `team_tags` in `tools/build_defs/common.bzl` calls `fail()` when it is empty, which stops every command. The negative cases `missing-team-attr` and `missing-team-build` check both paths.

### Kotlin policy needs a private options target

`rules_kotlin` takes warnings-as-errors through a `kt_kotlinc_options` target. The Kotlin macros create `<name>_kotlinc_opts` and remove `kotlinc_opts` from the macro's API, so a team cannot opt out.

## gRPC and protobuf (codegen spike)

### grpc-java and protobuf share `@maven`

**Symptom:** analysis fails on `@maven//:org_codehaus_mojo_animal_sniffer_annotations`, or Bazel warns about layering.
**Fix:** in `maven.install`, set `known_contributing_modules = ["grpc-java", "protobuf"]` and add `org.codehaus.mojo:animal-sniffer-annotations:1.24`, which grpc-java needs but does not declare into the shared repo.

### Go needs both proto compilers

**Symptom:** `undefined: EchoRequest` in generated Go.
**Fix:** pass both `@rules_go//proto:go_proto` and `@rules_go//proto:go_grpc_v2` as compilers. `go_grpc_v2` alone omits the message types.

### Python needs `protobuf` listed with `grpcio`

**Symptom:** `ModuleNotFoundError: google` at runtime.
**Fix:** add the pip `protobuf` package to the `py_binary` deps next to `grpcio`.

### Gazelle needs a root BUILD file

`go_deps.from_file` fails unless a root `BUILD.bazel` exists. The repo has one that defines the `//:gazelle` target.

### Load paths that worked

`@protobuf//bazel:{proto,cc_proto,java_proto,py_proto}_library.bzl`, `@grpc//bazel:cc_grpc_library.bzl` (with `grpc_only = True` and a `cc_proto` dep), `@grpc-java//:java_grpc_library.bzl`, `@rules_go//proto:def.bzl` for `go_proto_library`, and `@grpc//bazel:python_rules.bzl` for `py_grpc_library`.

### javac strict deps rejects message classes

**Symptom:** a Java or Kotlin target that depends on `<name>_java_grpc` fails with a strict-deps error on the message classes.
**Cause:** `java_grpc_library` exports the service stubs, not the message classes or the protos they depend on.
**Fix:** in `analytics_proto`, `<name>_java_grpc` is a `java_library` that re-exports the stubs, the messages and the dependency protos' Java targets.

### Generated Python stubs hide each other

**Symptom:** `query_pb2` cannot import `common_pb2`.
**Cause:** Bazel creates an `__init__.py` in each generated stub's own `_virtual_imports/<name>_proto` root. The first root on `sys.path` then hides the others.
**Fix:** `build --incompatible_default_to_explicit_init_py` in `.bazelrc`. `analytics.v1` then merges as a namespace package.

### Kotlin 2 does not warn on an unused local `val`

**Symptom:** a negative test for Kotlin `-Werror` that declared an unused `val` compiled cleanly.
**Cause:** Kotlin 2 no longer reports unused locals as warnings.
**Fix:** use a real warning, such as a useless safe call or a deprecated API. `tests/negative/werror_kt` does this.

### gRPC 1.84 headers trip `-Werror`

**Symptom:** C++ services fail to compile under `-Wall -Wextra -Werror` with a deprecated-declarations error from gRPC headers.
**Cause:** the headers call a deprecated absl function.
**Fix:** `analytics_grpc_service` adds `-Wno-error=deprecated-declarations` for clang and gcc on C++ services. It is scoped to the service macro, not the C++ wrapper.

### Gazelle `map_kind` alone rewrote proto targets

**Symptom:** after adding `map_kind`, `bazel run //:gazelle` rewrote proto targets.
**Cause:** Gazelle's proto support was still active and did not know that `analytics_proto` owns the generated Go packages.
**Fix:** in the root `BUILD.bazel`, set `# gazelle:proto disable_global`, add `# gazelle:resolve go ...` lines that map each generated Go package to its `*_go_grpc` target, and exclude `tools` and `tests`. Gazelle also cannot invent the required `team`, so add it by hand.

### A package that fails to load breaks `bazel build //...`

**Symptom:** a negative-test BUILD file with a loading error turned `bazel build //...` red, although its targets were tagged `manual`.
**Cause:** `manual` skips building a target, not loading its package.
**Fix:** loading-error fixtures live as `tests/negative/loading_fixtures/*.BUILD`. `scripts/check_negative.sh` stages each into a throwaway package for the run and removes it on exit. Do not run `bazel build //...` while the script runs.

### Cold gRPC build cost

A cold build of C++ plus Java gRPC stubs took 215 s and 4,598 actions on my machine. It is the baseline for the M2 cache work. Do not treat it as a benchmark; it is one run.

## Packaging

- `rules_python` sets up a dangling `_x.venv/bin/python3` symlink in runfiles. The tarball tool skips it with a warning.
- Python binaries bring their whole runfiles tree into the package, so `analytics.tar` is about 308 MB, and the packaged CLI cannot run from the unpacked tarball. See [Packaging Rule](Packaging-Rule).
- `kt_jvm_binary` has no `<name>_deploy.jar`. `//release:gateway_deploy.jar` comes from a `java_binary` over the Kotlin gateway library.

## Planning

A spurious cross-phase dependency edge in the tracker left a task blocked with no way to unblock it. Check new dependency edges with `bd show` before relying on them.

## Open items

- The gateway binds all network interfaces and has no `--host` flag. M2's hermeticity work must change that.
- The `gcc` and `msvc` warning branches of the C++ macros are untested.
