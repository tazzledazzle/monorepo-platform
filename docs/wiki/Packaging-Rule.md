# Packaging Rule

`analytics_package` (in `tools/build_defs/package.bzl`) bundles binaries into one reproducible tarball with a manifest. It is a hand-written rule. `rules_pkg` is the production choice; this rule exists to practice providers, depsets, runfiles, and exec-configured tools.

## Usage

```python
load("//tools/build_defs:package.bzl", "analytics_package")

analytics_package(
    name = "analytics",
    components = {"//engine/service": "engine"},  # label -> install subdirectory
    deps = [],                                    # nest other analytics_package targets
)
```

| Attribute | Type | Meaning |
|---|---|---|
| `components` | label-keyed string dict | Target to install subdirectory. |
| `deps` | label list | Other `analytics_package` targets to nest. Must provide `AnalyticsPackageInfo`. |

The real target is `//release:analytics`:

```python
analytics_package(
    name = "analytics",
    components = {
        "//engine/service:engine": "engine",
        ":gateway_deploy.jar": "gateway",
        "//ingest/agent/main:agent": "agent",
        "//reports/cli:cli": "cli",
    },
)
```

`:gateway_deploy.jar` is the implicit output of `//release:gateway`, a `java_binary` over the Kotlin gateway library (`main_class = analytics.gateway.MainKt`). The spec names `//server/gateway:gateway_deploy.jar`, but `kt_jvm_binary` has no deploy jar, so that label does not exist. Build it with `bazel build //release:analytics`. The rule is also exercised by fixtures in `tools/build_defs/tests/package_fixture/`.

## Outputs

- `<name>.tar`
- `<name>.MANIFEST.json`: a sorted list of `{path, sha256, label}`, one entry per packaged file.

## How it works

1. For each component the rule takes `DefaultInfo.files` plus the default runfiles. Each file lands at `<subdir>/<short_path>`. Files from external repos go under `<subdir>/external/<repo>/...`.
2. It returns an `AnalyticsPackageInfo` provider whose `files_by_path` is a depset. Nested packages contribute through `deps`, so the depset merges transitively without flattening early.
3. It writes a spec file, one JSON object per line, and runs the `make_tarball` tool. The tool is a `py_binary` with `cfg = "exec"`, so it builds for the machine that runs the action.

## Reproducibility

`make_tarball.py` sorts entries by install path, sets mtime to 0, uid and gid to 0, empties user and group names, normalizes modes to 0644 or 0755, and writes plain GNU tar (no gzip header, no PAX timestamps).

Check it yourself:

```sh
tools/build_defs/packaging/check_reproducible.sh
```

The script builds a target in two clean output bases and compares sha256 of the tar and manifest. On my machine (macOS arm64) the fixture package `pkg_nested` produced identical outputs. The result matters later because remote-cache hits depend on identical bytes (M2).

## Conflicts

If two different source files map to the same install path, the build fails and names both source labels. The same file reached twice through nesting is not a conflict.

## Limitations

- A `py_binary` ships with its entire runfiles file set (interpreter, sources, pip packages), each file under `<subdir>/<short_path>`.
- Runfiles symlinks and root symlinks are not packaged.
- Tree artifacts (directories) are not packaged. The tool exits with an error for anything that is not a regular file.
- Dangling symlink artifacts are skipped with a warning. An example is rules_python's `_x.venv/bin/python3`. 
- The packaged Python CLI cannot run from the unpacked tarball. The `py_binary` launcher expects Bazel's runfiles tree, which the tarball does not recreate. The package test does run the unpacked Go agent and C++ engine, and checks that the gateway jar names `analytics.gateway.MainKt` as its main class; it does not run the jar or the CLI.
- `analytics.tar` is about 308 MB (308,459,520 bytes in my build). By size: the CLI's Python runfiles about 190 MB, the engine about 90 MB, the gateway jar about 16 MB, the agent about 10 MB.
- Nothing has been tested on Linux or Windows.

## Tests

- `//tools/build_defs/packaging:make_tarball_test` tests the tool.
- `//tools/build_defs/tests/package_fixture:package_rule_test` checks contents, sorting, nesting, normalized metadata, and manifest checksums.
- `//release:package_test` unpacks the real `analytics.tar`, verifies every manifest checksum, checks that the expected executables are present, and starts the unpacked engine and agent.
- `//tools/build_defs/tests/analysis:package_provider_test` and `:package_nesting_test` check the provider and nesting at analysis time.
