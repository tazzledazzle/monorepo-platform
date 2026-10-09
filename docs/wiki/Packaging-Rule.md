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

The real `//release:analytics` target is planned (Phase 7). The rule is exercised today by fixtures in `tools/build_defs/tests/package_fixture/`.

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
- Dangling symlink artifacts are skipped with a warning. An example is rules_python's `_x.venv/bin/python3`. I have not tested whether a packaged Python binary runs from an unpacked tarball.
- Nothing has been tested on Linux or Windows.

## Tests

- `//tools/build_defs/packaging:make_tarball_test` tests the tool.
- `//tools/build_defs/tests/package_fixture:package_rule_test` checks contents, sorting, nesting, normalized metadata, and manifest checksums.
- Planned: `//release:package_test`, which unpacks the real `analytics.tar` and verifies every manifest checksum.
