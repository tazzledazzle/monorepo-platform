# Adding a target

(Other sections of this guide are added by the macro stories; this one covers packaging.)

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
