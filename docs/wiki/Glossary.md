# Glossary

| Term | Meaning here |
|---|---|
| Analysistest | A Skylib test that checks a rule's or macro's analysis-time output (providers, actions, attributes). Planned for Phase 7. |
| `apple_support` | Bazel module that supplies the Xcode-aware C++ toolchain on macOS. |
| Bazelisk | Launcher that downloads the Bazel version in `.bazelversion`. |
| Beads (`bd`) | The issue tracker that holds the plan. See `docs/planning-in-beads.md`. |
| Blitz | Tableau's migration to a unified Bazel monorepo build platform. This project imitates it in miniature. |
| Bzlmod | Bazel's module system. `MODULE.bazel` replaces `WORKSPACE`. |
| Classic macro | A `def` function that calls rules. Its generated targets are visible to others. Contrast with symbolic macro. |
| Configurable attribute | An attribute that may hold a `select()`. Macros that inspect a value need it non-configurable. |
| Depset | Bazel's efficient, deduplicating collection type for files and other data across rules. |
| Exec configuration | The configuration for tools that run during the build, as opposed to the target platform. |
| Gate | Evidence required to close a phase. |
| Gazelle | BUILD-file generator for Go. `map_kind` makes it emit our macros. Not configured yet. |
| `inherit_attrs` | `macro()` option that copies attributes from a rule or macro. Does not accept plain functions. |
| Known contributing modules | `maven.install` setting that names modules adding to the shared `@maven` repo. |
| Milestone (M1-M4) | A group of phases with its own spec and working result. |
| Phase | A numbered epic (1 to 17) in the tracker, with a goal and acceptance criteria. |
| Platform layer | `tools/build_defs`: the macros and rules that product teams load. |
| Provider | A struct a rule returns for other rules to read, such as `AnalyticsPackageInfo`. |
| Runfiles | Files a binary needs at run time, tracked by Bazel separately from its main output. |
| Symbolic macro | A macro defined with `macro()`. It has typed attributes, and its internal targets are private. |
| `team` tag | Tag `team:<name>` added by every wrapper. Planned use: mapping changes to owners in `bazel query`. |
