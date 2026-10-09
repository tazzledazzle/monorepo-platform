# Platform Macros

Fifteen wrappers in `tools/build_defs`: library, binary, and test for each of C++, Java, Kotlin, Go, and Python. Product teams load these instead of the raw rules.

## Policy common to all

Defined in `tools/build_defs/common.bzl`.

| Policy | Behavior |
|---|---|
| `team` | Required, non-configurable. Adds the tag `team:<name>`. |
| Test size | Tests default to `size = "small"`, so slow tests must opt in visibly. |
| Visibility | Passed through unchanged. The macro never widens it. |
| Unset attributes | Dropped before the call, so the wrapped rule's own defaults apply. |

Repo-wide toolchain flags are not here. They live in `.bazelrc`.

## Reference

| Macro | Load from | Wraps | Policy added |
|---|---|---|---|
| `analytics_cc_library`, `_binary`, `_test` | `cc.bzl` | `cc_library`, `cc_binary`, `cc_test` | Prepends `-Wall -Wextra -Werror` (clang, gcc) or `/W4 /WX` (msvc-cl) to `copts` via `select()`. Tests add `@googletest//:gtest_main`. |
| `analytics_java_library`, `_binary`, `_test` | `java.bzl` | `java_library`, `java_binary`, `java_test` | Prepends `-Xlint:all -Werror` to `javacopts`. Tests add `@maven//:junit_junit` and derive `test_class`. |
| `analytics_kt_library`, `_binary`, `_test` | `kt.bzl` | `kt_jvm_library`, `kt_jvm_binary`, `kt_jvm_test` | Warnings are errors through a private `<name>_kotlinc_opts` target. `kotlinc_opts` is removed from the API. Tests add JUnit and derive `test_class`. |
| `analytics_go_library`, `_binary`, `_test` | `go.bzl` | `go_library`, `go_binary`, `go_test` | `team` tag and test size default only. |
| `analytics_py_library`, `_binary`, `_test` | `py.bzl` | `py_library`, `py_binary`, `py_test` | Tests run under pytest through `pytest_main.py`; the macro passes test files as arguments. |

### Derived `test_class` (Java, Kotlin)

An explicit `test_class` wins. Otherwise the macro finds the source file named after the target (`CatalogTest` finds `CatalogTest.java` or `.kt`) and takes the package from the path below a source root (`src/test/java/`, `src/test/kotlin/`, and others). If nothing matches, the build fails at load time and names the target. Because the macro inspects `srcs`, the test macros declare `srcs` non-configurable.

### Python tests

`analytics_py_test` adds `pytest_main.py` as `main`, adds `@pip//pytest` to `deps`, and turns each source into a `$(rootpath ...)` argument. `srcs` is non-configurable for that reason. The shim writes JUnit XML to `XML_OUTPUT_FILE` when Bazel sets it.

## Why explicit attributes instead of `inherit_attrs`

`macro(inherit_attrs = X)` accepts only a rule or a macro symbol. The wrappers differ in what the underlying library exports:

| Wrapped | What the ruleset exports | Result |
|---|---|---|
| `cc_*` (rules_cc) | Plain functions | `inherit_attrs = "common"` plus an explicit attribute list |
| `java_*` (rules_java) | Plain functions | Same |
| `py_*` (rules_python) | Plain functions | Same |
| `go_binary` (rules_go) | Plain function | Same |
| `go_library`, `go_test` | Rules | `inherit_attrs = go_library` and `go_test`; all attributes inherited |
| `kt_jvm_*` (rules_kotlin) | Rules | Full inheritance |

With `"common"`, a macro inherits only the attributes every rule shares (such as `tags`, `testonly`, `features`). Language-specific attributes come from the lists in each `.bzl` file.

The cost: if a team needs an attribute the list omits, the macro rejects it. The fix is to add it to the list (`_CC_ATTRS`, `_JAVA_ATTRS`, `_PY_ATTRS`, `_GO_BINARY_ATTRS`), not to bypass the wrapper.

## Example

From `tools/build_defs/tests/samples/kt/BUILD.bazel`:

```python
load("//tools/build_defs:kt.bzl", "analytics_kt_binary", "analytics_kt_library", "analytics_kt_test")

analytics_kt_library(
    name = "lib",
    srcs = ["src/main/kotlin/sample/Greeter.kt"],
    team = "platform",
)

analytics_kt_binary(
    name = "bin",
    srcs = ["src/main/kotlin/sample/Main.kt"],
    main_class = "sample.MainKt",
    team = "platform",
    deps = [":lib"],
)
```

Each sample directory also holds a target tagged `manual` that must fail to build under the policy (for example `//tools/build_defs/tests/samples/cc:warns`). The automated negative-test script that runs them is planned (Phase 7).

## Not built yet

- `analytics_grpc_service` (Phase 3). `analytics_proto` is built: see `tools/build_defs/proto.bzl` and `proto/analytics/v1/BUILD.bazel`.
- `analysistest` suites for each macro (Phase 7). Today the macros are covered by the sample targets and tests.
- Gazelle `map_kind` configuration, so `bazel run //:gazelle` generates the Go macros (Phase 7). The root `BUILD.bazel` defines the Gazelle target but does not set `map_kind` yet.
- A lint that rejects product BUILD files loading from anywhere but `//tools/build_defs` (Phase 14). For now the rule is a convention.
