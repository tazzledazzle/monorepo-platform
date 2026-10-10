# SPIKE: surfacing dependency-resolution conflicts programmatically (UC-04)

Issue: `tlow-6p3.5.1.1`. Date: 2026-10-09. Bazel 9.3.0, Bzlmod, macOS arm64, uv 0.8.0, Go 1.26.2.
Everything below was run in a scratch clone with its own output base; no tracked repo file was changed.

UC-04: an engineer adds a dependency that conflicts with a pinned version; the build reports the conflict with
the full dependency path, before merge, never at runtime.

## Verdicts

| Ecosystem | Detectable? | Best signal | Path in message? | Hard failure out of the box? |
|---|---|---|---|---|
| Bzlmod | Yes, fully | `--check_direct_dependencies=error`, `--lockfile_mode=error`, `--check_bazel_compatibility=error`, yanked check (default) | Yes via `bazel mod explain` / `mod graph --verbose --include_unused` ("cause ...") | Yes, exit 48 with the flags |
| Maven | Yes | lock `conflict_resolution` map + `fail_if_repin_required` (default true under Bzlmod) | Not in message; derivable from lock `dependencies` map | Lock drift yes (exit 1). Version conflicts only a `DEBUG:` line, so we must parse |
| pip | Partly | re-resolve and diff (`uv pip compile` over a copy of the lock) | uv prints the conflicting requirement chain | uv fails on unsatisfiable input; Bazel never checks |
| Go | Yes, with caveats | `go list -m all` / `go mod graph` MVS diff against go.mod, `go mod verify`, `bazel mod tidy` for `use_repo` | `go mod graph` edges give the requirer | `go mod tidy -diff` is unusable here (see below) |

No ecosystem lacks a detection route, so there is no escalation.

## 1. Bzlmod

### Commands and evidence

Reproduction: lower the root pin `rules_cc` from 0.2.26 to 0.2.22 (a transitive module requires 0.2.26).

Default behaviour is only a warning, exit 0 (this is the "root requires 0.2.22 but got 0.2.26" message):

```
WARNING: For repository 'rules_cc', the root module requires module version rules_cc@0.2.22, but got rules_cc@0.2.26 in the resolved dependency graph. Please update the version in your MODULE.bazel or set --check_direct_dependencies=off
...
INFO: Build completed successfully, 0 total actions     # exit 0
```

Turned into a hard failure with `--check_direct_dependencies=error`:

```
$ bazel build //engine/... --nobuild --check_direct_dependencies=error
ERROR: For repository 'rules_cc', the root module requires module version rules_cc@0.2.22, but got rules_cc@0.2.26 in the resolved dependency graph. Please update the version in your MODULE.bazel or set --check_direct_dependencies=off
ERROR: Error computing the main repository mapping: Direct dependency check failed.
# exit 48
```

Other checks, all verified:

- Lockfile drift (root `bazel_dep` of `bazel_skylib` changed 1.9.2 to 1.9.1), `--lockfile_mode=error`:
  `ERROR: ... Missing checksum for registry file https://bcr.bazel.build/modules/bazel_skylib/1.9.1/MODULE.bazel not permitted with --lockfile_mode=error. Please run 'bazel mod deps --lockfile_mode=update' to update your lockfile.` exit 48.
  Unchanged tree with the flag: exit 0, 3.5 s warm.
- `--check_bazel_compatibility=error` with `bazel_compatibility = [">=7.0.0", "<9.0.0"]` on the root:
  `ERROR: Bazel version 9.3.0 is not compatible with module "<root>" (bazel_compatibility: [>=7.0.0, <9.0.0])` exit 48.
- Yanked versions fail by default (no flag needed). Pinning `protobuf` 36.0:
  `ERROR: ... Yanked version detected in your resolved dependency graph: protobuf@36.0, for the reason: macOS prebuilt toolchain integrity mismatch ... Use 36.0.bcr.1 instead.` exit 48.
  Escape hatch: `--allow_yanked_versions=protobuf@36.0` (do not use in CI). Yanked selections are also recorded in
  `MODULE.bazel.lock` under `selectedYankedVersions`, so lock-diff review sees them.

### Full dependency path

- `bazel mod explain <module>` prints every path from the root to the module. In the baseline,
  `bazel mod explain rules_cc` lists e.g. `apple_support -> rules_cc`, `grpc -> envoy_api -> cel-spec -> rules_cc`.
- `bazel mod graph --verbose --include_unused` annotates each edge with the version change and the modules that caused it:
  `rules_cc@0.0.17 (to 0.2.26, cause <root>, apple_support@2.10.0)`, `bazel_skylib@1.9.2 (*) (was 1.9.0, cause <root>)`.
  This is the "requested vs selected" report the UC-04 story asks for.
- `bazel mod graph --output json` gives a machine-readable resolved graph (keys `key,name,version,dependencies,...`);
  it contains only resolved versions, so use the text `--verbose --include_unused` form to get "was X".
- Not every upgrade is a conflict: MVS upgrades are normal. Only two cases matter: (a) the root's own pin is below the
  selected version (`--check_direct_dependencies=error` catches it), (b) a transitive module changed version relative to a
  committed baseline (see design: `deps/bzlmod_resolved.txt` snapshot).

### Surprises

- On this repo every `bazel mod graph|explain|deps` run prints
  `ERROR: module extension @@googleapis+//:extensions.bzl%switched_rules does not generate repository "com_google_googleapis_imports", yet it is imported ... envoy_api ...` and exits 2,
  even in the pristine tree, while still printing complete output and valid JSON. `bazel build` is unaffected (exit 0).
  The detector must therefore not trust the exit code of `mod graph`; use it only to render paths, and gate on the
  `build --nobuild` flags above. Worth a separate bug/issue (it comes from grpc -> envoy_api -> googleapis).
- `bazel build` with a stale use_repo only warns (see Go).

### CI command

```
bazel build //... --nobuild --check_direct_dependencies=error --lockfile_mode=error --check_bazel_compatibility=error
```
About 3.5 s warm (loads and analyses the graph; `--nobuild`). On failure, print `bazel mod explain <module>` for the module named in the message.
Better to put the three flags in `.bazelrc` under `common:ci` so every CI command shares them.

## 2. Maven (rules_jvm_external 7.1)

### Evidence

1. Lock drift. Adding `org.hamcrest:hamcrest-core:1.1` to `maven.install` without repinning, then
   `bazel build @maven//:all --nobuild` (exit 1, 0.7 s):

```
DEBUG: .../rules_jvm_external+/private/rules/coursier.bzl:964:18: Found duplicate artifact versions
    org.codehaus.mojo:animal-sniffer-annotations has multiple versions 1.24, 1.27
Please remove duplicate artifacts from the artifact list so you do not get unexpected artifact versions
ERROR: .../coursier.bzl:678:21: An error occurred during the fetch of repository 'rules_jvm_external++maven+maven':
Error in fail: maven_install.json contains an invalid input signature (changes: org.hamcrest:hamcrest-core: added) and must be regenerated. ... To generate a new maven_install.json and re-pin the artifacts, please run:
 REPIN=1 bazel run @maven//:pin
```
   `fail_if_repin_required` defaults to `True` in the Bzlmod extension (`private/extensions/maven.bzl:93`), so no config is needed.
   The message names the changed artifact ("changes: ... added").

2. Version conflict. After `REPIN=1 bazel run @maven//:pin` (7.9 s) the lock gains a `conflict_resolution` map, the
   machine-readable "requested -> selected" record:

```
"conflict_resolution": {
 "com.google.guava:failureaccess:1.0.1": "com.google.guava:failureaccess:1.0.3",
 "com.google.j2objc:j2objc-annotations:2.8": "com.google.j2objc:j2objc-annotations:3.1",
 "io.opencensus:opencensus-api:0.31.0": "io.opencensus:opencensus-api:0.31.1",
 "org.codehaus.mojo:animal-sniffer-annotations:1.24": "org.codehaus.mojo:animal-sniffer-annotations:1.27",
 "org.hamcrest:hamcrest-core:1.1": "org.hamcrest:hamcrest-core:1.3"
}
```
   The first four entries exist in the committed lock today. The last is the injected conflict (root asked for 1.1,
   junit 4.13.2 needs 1.3, coursier picked 1.3). Nothing failed or warned at build time for hamcrest, so the lock
   is the only place that records it.

3. Full path. The lock's `dependencies` map is parent -> children, so a BFS gives the chain. For the existing
   conflict: `org.codehaus.mojo:animal-sniffer-annotations <- io.grpc:grpc-stub <- com.google.s2a.proto.v2:s2a-proto`.
   (`bazel query` on `@maven//...` shows the same edges as `deps`, but the lock needs no fetch.)

4. `version_conflict_policy = "pinned"` (attr on `maven.install`; `"default"` is the default): with it, the injected
   `hamcrest-core:1.1` stayed at 1.1 and left `conflict_resolution`, i.e. the root pin wins. It does NOT apply to
   artifacts contributed by other modules: `animal-sniffer-annotations` 1.24 (ours) vs 1.27 (grpc-java's contribution via
   `known_contributing_modules`) still resolved to 1.27 and the "Found duplicate artifact versions" DEBUG still printed.
   There is no `resolve_conflicts` attribute in 7.1; `version_conflict_policy` is the only policy knob, and
   `resolver = "maven"/"gradle"` change the algorithm.

### Surprise: the repo already carries a stale pin

`MODULE.bazel` pins `animal-sniffer-annotations:1.24` with a comment saying grpc-java needs it, but the lock selects 1.27,
and every fresh fetch prints the "duplicate artifact versions" DEBUG. Either bump the pin to 1.27 or drop it. This is the
first real finding the detector would report.

### Detection design

- Lock current: `bazel build @maven//:all --nobuild` fails with exit 1 and the REPIN hint (or `--repo_env=...`; no extra flag needed).
- Conflicts: parse `maven_install.json["conflict_resolution"]`, compare against a committed allowlist
  (`deps/maven_conflicts_allowed.txt`) and fail on any entry not on it, printing requested, selected, and the path from
  the lock `dependencies` map. Run it on the committed lock (no network, ms) and on a freshly repinned copy (to catch a
  stale lock whose input signature still matches).
- The DEBUG warning is not a reliable gate (DEBUG, only on a fetch, hidden by cache).

## 3. pip (requirements.in -> requirements_lock.txt)

### Evidence

Genuine conflict (add `grpcio-tools==1.62.0`, which needs `protobuf<5`, against the pinned `protobuf==7.36.2`):

```
$ uv pip compile req.in --python-version 3.12 --generate-hashes -o out.txt
  × No solution found when resolving dependencies:
  ╰─▶ Because grpcio-tools==1.62.0 depends on protobuf>=4.21.6,<5.0.dev0 and
      you require protobuf==7.36.2, we can conclude that your requirements and
      grpcio-tools==1.62.0 are incompatible.
      And because you require grpcio-tools==1.62.0, we can conclude that your
      requirements are unsatisfiable.
# exit 1, 0.3 s
```
uv's message is the dependency path (transitive chains are printed "A depends on B depends on C").

Stale lock (new dep `requests` in `.in`, lock not regenerated): recompiling gives a diff
(`+certifi`, `+charset-normalizer`, `+idna`, `+requests`, `+urllib3`). A lock that matches its `.in` recompiles to an empty diff (0.07 s).

Important: `uv pip compile -o <path>` reuses pins found in an existing `<path>`. Recompiling to a fresh path re-resolves to
latest and can differ for unrelated reasons. For the drift check, copy the lock to a temp file and compile with `-o` pointing at the copy.
Demonstrated: changing `tabulate` to `tabulate==0.9.0` in a copy of the `.in` and recompiling over a copy of the lock
produced `< tabulate==0.10.0` / `> tabulate==0.9.0`.

### What Bazel does (nothing)

`pip.parse` trusts the lock. Editing `requirements_lock.txt` to `tabulate==0.9.0` while leaving the hashes:
`bazel build @pip//:all` succeeds and the installed wheel is still `tabulate-0.10.0` (the hash picks the file; the version text is ignored).
`requirements.in` is not an input to Bazel at all. A hand-edited, inconsistent or stale lock is invisible to Bazel.
`uv pip install --dry-run --require-hashes` also does not catch a version/hash mismatch (it resolves from the lock text), so
it is not a substitute for the recompile check.

### CI command

```
cp requirements_lock.txt "$tmp" && uv pip compile requirements.in --python-version 3.12 --generate-hashes -q -o "$tmp"
diff <(grep -v '^#' requirements_lock.txt) <(grep -v '^#' "$tmp")
```
Exit nonzero on resolver failure (conflict, uv prints the chain) or on a non-empty diff (stale lock). Needs network on a cold uv cache; uv version
should be pinned because the header comment embeds the command.

## 4. Go

### Evidence

- `go mod verify` (0.7 s): `all modules verified`, exit 0. Verifies module cache hashes against go.sum; catches tampering, not version conflicts.
- `go mod tidy -diff` is NOT usable as-is. In the pristine tree it exits 1 because Go sources import Bazel-generated protobuf packages
  that do not exist on disk (`cannot find module providing package example.com/three_languages_one_workspace/proto/analytics/v1/commonpb`...), and
  it also wants to move `google.golang.org/protobuf` to `// indirect`. `go mod tidy -e -diff` runs but the baseline diff is non-empty noise.
  Either exclude or generate those packages, or avoid tidy and compare only module versions (below).
- MVS conflict (lower `golang.org/x/net` from v0.57.0 to v0.20.0 while grpc v1.84.0 requires v0.57.0):
  - `go list -m all` (readonly) fails exit 1: `go: golang.org/x/net@v0.20.0: missing go.sum entry for go.mod file; to add it: go mod download golang.org/x/net`.
    (If go.sum already has the lower version the failure would not trigger, so do not rely on it.)
  - Reliable: in a temp copy of go.mod/go.sum run `GOFLAGS=-mod=mod go list -m all` and diff the go.mod. Go rewrites the
    requirement to the MVS-selected version; the diff is the conflict report:
    `-  golang.org/x/net v0.20.0 // indirect`  `+ golang.org/x/net v0.57.0 // indirect`.
  - Path: `go mod graph | awk '$2 ~ /^golang.org\/x\/net@v0.57.0$/'` gives
    `google.golang.org/grpc@v1.84.0 -> golang.org/x/net@v0.57.0` (the requirer that forces it). `go mod why -m <mod>` gives the package chain.
- `go list -m -u all` (1.7 s) lists newer versions available (informational, not conflicts). Use it for a weekly "outdated" report, not a gate.
- Bazel side (Gazelle `go_deps`, `use_repo` drift). Removing `org_golang_google_protobuf` from `use_repo(go_deps, ...)`:
  `bazel build //ingest/...` still succeeds (exit 0) and prints only a WARNING:

```
WARNING: .../MODULE.bazel:45:24: The module extension go_deps defined in @gazelle//:extensions.bzl reported incorrect imports of repositories via use_repo():

Not imported, but reported as direct dependencies by the extension (may cause the build to fail):
    org_golang_google_protobuf

Fix the use_repo calls by running 'bazel mod tidy'.
```
  `bazel mod tidy` (2.3 s warm) rewrites it (`INFO: Updated use_repo calls for @gazelle//:extensions.bzl%go_deps`) and returns 0, so the check is
  `bazel mod tidy && git diff --exit-code MODULE.bazel`. Same command covers `use_repo` drift for maven/pip extensions.

### CI commands

```
go mod verify
d=$(mktemp -d); cp go.mod go.sum "$d"; (cd "$d" && GOFLAGS=-mod=mod go list -m all >/dev/null) ; diff go.mod "$d/go.mod"
bazel mod tidy && git diff --exit-code -- MODULE.bazel
```
(the second needs the module cache, so network on a cold CI runner; go.sum is already committed.)

## Gaps: conflicts nothing catches

- Bzlmod: a transitive module upgraded by MVS is silent unless we diff against a baseline; semantic incompatibility between
  the old API and the selected version is invisible (only builds/tests tell). `bazel mod graph` exit code is unreliable here (googleapis extension error).
- Maven: coursier's choice between two non-root versions is silent and recorded only in the lock; the DEBUG line is the only live signal;
  `pinned` policy does not override other modules' contributions; classifier/type variants are not compared.
- pip: Bazel never validates the lock; a hand-edited lock builds; wheels not present for the platform are only found at fetch time on that platform
  (we compile for one `--python-version`, no `--universal`); `requirements.in` is invisible to Bazel.
- Go: MVS only goes up so conflicts are upgrades, not failures; `go mod tidy` is blocked by generated pb packages; Bazel's go_deps ignores go.mod entries not imported by BUILD files.
- Cross-ecosystem: the same library in several ecosystems (protobuf as Bzlmod module 36.2, Maven protobuf-java, PyPI `protobuf==7.36.2`, Go
  `google.golang.org/protobuf`) can drift apart; nothing checks that they stay compatible. A small table of "must-align" families would need to be hand-maintained.
- Runtime-only conflicts (shaded jars, duplicate classes on a classpath) are outside all of these.

## Proposed `scripts/check_deps.sh`

Name: the story currently says `scripts/check_conflicts.sh`; keep the lock-currency half (tlow-6p3.4) and conflict half (tlow-6p3.5) in one
script with sub-checks so CI runs one command.

```
scripts/check_deps.sh [--only bzlmod|maven|pip|go] [--report FILE] [--update-baseline] [--keep-going] [-v]
```

Exit codes: 0 all clean; 1 conflict (a version difference that needs a human decision); 2 stale lock or lock/pin drift (fix by repinning);
3 tooling/environment error (offline, uv or go missing); 4 yanked or incompatible Bazel (policy). With `--keep-going` run every ecosystem and
exit with the highest code. Each failing check prints one block: `ECOSYSTEM  module  requested -> selected  via  A -> B -> C` and the
repair command (`REPIN=1 bazel run @maven//:pin`, `uv pip compile ...`, `bazel mod tidy`, `bazel mod deps --lockfile_mode=update`).

Checks:
1. bzlmod: `bazel build //... --nobuild --check_direct_dependencies=error --lockfile_mode=error --check_bazel_compatibility=error`; on failure, parse the module out of the message and append `bazel mod explain <module>`.
   Baseline: `bazel mod graph --verbose --include_unused` diff vs `deps/bzlmod_resolved.txt` (resolved name@version list, regenerate with `--update-baseline`); ignore the exit status of `mod graph`.
2. maven: build `@maven//:all --nobuild` (stale lock), then `conflict_resolution` in the lock vs `deps/maven_conflicts_allowed.txt`, path via the lock graph (python3 stdlib, no deps).
3. pip: recompile-over-a-copy diff as above.
4. go: `go mod verify`, temp-copy `go list -m all` go.mod diff with `go mod graph` path, `bazel mod tidy` + `git diff --exit-code`.

Speed: bzlmod ~4 s, maven <1 s warm (repin check ~8 s), pip <1 s, go ~3 s. Whole script is well under 30 s warm, so it fits a pre-merge job and a pre-commit-friendly `--only` mode.

Test fixtures (tlow-6p3.5.1.4): each reproduction above is a one-line patch to MODULE.bazel / maven artifacts / requirements.in / go.mod;
apply to a temp copy and assert exit code and message. The exit codes in the section above are the contract.

## Scope notes for follow-up stories

- `tlow-6p3.5.1.2` (Bzlmod detector): mostly flags plus path rendering; add the googleapis-extension exit-code caveat.
- `tlow-6p3.5.1.3` (maven/pip/go): maven reads the lock file (no resolver run); pip recompile-over-copy; go avoids `tidy`.
- `tlow-6p3.4.*` (pinning): the lock-currency checks are the same flags/commands; share the script.
- New small items: resolve the stale `animal-sniffer-annotations:1.24` pin; investigate the googleapis `switched_rules` error that makes `bazel mod` exit 2.
