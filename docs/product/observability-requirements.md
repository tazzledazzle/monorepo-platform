# Build observability: product requirements

Theme epic: `tlow-6ub.10`. Spike: `tlow-hq0`. Related decision: `tlow-6ub.6.1.1` (remote cache).
Status: draft for the user's review. Written 2026-10-09.

## Requirement

> I want my toolchain to produce build scans for any build using the platform.

A build scan is a shareable web record of one build: a URL instead of a log file. It works like a crash dump for builds. The reference product is Develocity Build Scans for Gradle and Maven. Bazel's mechanism is the Build Event Protocol (BES).

A scan records one build. It is not an application profiler. Cross-build analytics belong to the server product around it and are later work here.

## Personas

| Persona | Situation | Needs from a scan |
|---|---|---|
| Platform engineer | Owns `.bazelrc`, macros, toolchains. Judges whether a platform change made builds slower. | Timeline, time by action type, cache hit rate, the baseline to compare against. |
| Product developer | A local build fails or is slow and they do not know why. | The failing action and its output at the top of the scan. Why an action ran instead of hitting the cache. |
| CI engineer | Runs builds in CI (M3). Debugs failures they cannot reproduce. | Every CI build has a scan, tagged CI, with commit, branch, and machine details. |
| Reviewer | Reads a pull request check. | One link from the check to the scan. Pass or fail, duration, cache hit rate at a glance. |
| Supporter and requester | A developer says "my build is broken, send me the scan." Someone else investigates. | A link that opens without an account on the requester's machine and shows enough to diagnose. |

In this sandbox one person plays all five roles. The personas still decide which scan content matters.

## Jobs to be done

1. When my build fails, show me the failing action, its output, and what changed, so I can fix it without rerunning.
2. When my build is slow, show me where the time went and what limited parallelism, so I know what to change.
3. When an action ran that I expected to be cached, tell me why, so I can fix the cache key.
4. When I ask for help, let me give one link instead of pasting logs.
5. When I change the platform, let me compare a build before and after.
6. When I read a CI result, let me see the build behind it in one click.

## Scan content required (Develocity parity list)

| Area | Bazel equivalent |
|---|---|
| Timeline and parallelism | Invocation timeline, critical path, action concurrency |
| Performance breakdown | Time by action mnemonic (CppCompile, Javac, GoCompile), by target or package, by phase (loading, analysis, execution) |
| Cache and task outcomes | Per action: executed, cache hit (local or remote), and the reason it ran |
| Dependencies | Resolved external module versions and repositories, fetch time |
| Tests | Result, duration, attempts, flaky signal (pass after retry) |
| Environment | Bazel version, JDK, OS, Xcode, CPU count, CI or local, git commit and branch |
| Custom data | Tags and key-value pairs from `--build_metadata` |

## MVP and later

**MVP (this milestone, Phase 8):**

- Every platform build, local or CI, uploads a scan by default and prints its URL.
- The scan shows timeline, per-action timing, cache hit and miss counts, test results with durations, command line, tool versions, git commit and branch, and a CI-or-local tag.
- A down or unreachable backend never fails or noticeably slows a build.
- The privacy stance is written down and enforced by configuration.
- The M1 cold-build baseline (459 s) is saved as a scan.

**Later (not MVP; do not build now):**

- Cross-build analytics and trend dashboards.
- Flaky-test dashboards and history across builds. (Single-build flaky signals are in scope.)
- Predictive test selection.
- Alerting and notifications.
- Failure analytics across builds.
- PR check integration beyond a link. It waits for CI (Phase 13).
- Single sign-on and role-based access. Needed only with a shared server.

## Non-goals

- Profiling the application's runtime performance.
- Replacing `bazel` output for the developer who just wants the log.
- Collecting scans from builds that do not go through the platform.
- Building our own scan UI or BES server.
- Production-grade high availability of the scan backend.

## User stories

Priorities use MoSCoW, mapped to beads: Must = P0 or P1, Should = P2, Could = P3. P0 marks the stories that block everything else.
Each story is a beads issue under `tlow-6ub.10` with these acceptance criteria.

| # | Story | MoSCoW | Beads | Acceptance criteria |
|---|---|---|---|---|
| S1 | As a developer, I get a scan URL from a plain `bazel build` or `bazel test`, with no extra flags. | Must (P0) | P0 | 1. On a clean clone with the backend running, `bazel build //release/...` prints a scan URL on its last lines. 2. The URL opens in a browser and shows that invocation. 3. The flags live in the committed `.bazelrc`. |
| S2 | As a developer, I can opt out on my machine without editing tracked files. | Must (P1) | P1 | 1. A documented line in `user.bazelrc` disables upload. 2. With it set, no BES connection is attempted and no URL prints. 3. `user.bazelrc` stays untracked. |
| S3 | As any user, a down or unreachable backend never fails or noticeably slows my build. | Must (P0) | P0 | 1. With the backend stopped, `bazel build` exits 0 for a build that otherwise passes. 2. With the network off, same result. 3. Added wall time is at most 5 s versus a no-BES build, measured on a no-op build. 4. The build prints one warning, not an error. 5. Evidence is recorded in the issue. |
| S4 | As a reader of a scan, I see where it came from: git commit, branch, dirty or clean tree, and whether it ran in CI or locally. | Must (P1) | P1 | 1. The scan shows `GIT_COMMIT`, `BRANCH`, `ENV` (`local` or `ci`) as metadata. 2. A scan from a dirty tree is marked dirty. 3. Values are supplied by the workspace-status script or `--build_metadata`, with no per-user step. |
| S5 | As a developer with a slow build, I can find the slowest actions and the critical path from the scan alone. | Must (P1) | P1 | 1. The scan shows a timeline with concurrency. 2. It lists actions by duration, and by mnemonic. 3. A reviewer unfamiliar with the build names the slowest mnemonic and the critical path length within 5 minutes using only the scan. |
| S6 | As a developer, I see cache hits and misses, and why an action ran. | Must (P1) | P1 | 1. The scan shows counts of executed, local-cache, and remote-cache actions. 2. A second identical build shows the hit rate rise, with the numbers visible. 3. For an executed action, the scan shows the spawn strategy (sandboxed, worker, remote). If the backend cannot show why an action missed the cache, the gap is recorded in the issue. |
| S7 | As a developer, I see test results with durations, and a flaky signal when a test passes only after retry. | Must (P1) | P1 | 1. Each test target in `bazel test //...` appears with status and duration. 2. Failed test output is reachable from the scan. 3. A test that passes on a retry with `--flaky_test_attempts` is labelled flaky. |
| S8 | As the repo owner, I know what a scan contains and who can read it, and sensitive values are kept out. | Must (P1) | P1 | 1. A section in `docs/` lists scan contents and the readers. 2. A scan taken from a user whose home directory is `/Users/<name>` shows no username and no home path in the command line, environment, or metadata. 3. Environment variables are not uploaded, except an allowlist. 4. The backend binds to localhost only, or sits behind authentication. 5. No scan is on a public service. |
| S9 | As a platform engineer, I have a saved baseline scan of the M1 cold build to compare against. | Should (P2) | P2 | 1. A cold `bazel test //...` (empty output base) is scanned. 2. The scan shows about 8,559 actions and 34 executed tests, matching the 459 s README figure within 10%. 3. The URL or exported file is recorded in the issue and in the Phase 10 cache story as its comparison point. |
| S10 | As a CI engineer, every CI build publishes a scan, tagged CI, including successful ones. | Should (P2) | P2 | 1. A `config:ci` in `.bazelrc` sets the CI tag, a stable invocation identity, and a longer upload wait. 2. The config can be exercised locally with `--config=ci`. 3. Scans are published on success and on failure. |
| S11 | As a supporter, I can diagnose a "send me the scan" request from the link alone. | Should (P2) | P2 | 1. A short runbook in `docs/` says how to share a scan and what to check first. 2. A scan of a deliberately failing target (`tests/negative`) lets a second person identify the failing target and the error message from the link. |
| S12 | As the owner of the backend, I can start, stop, and wipe it with one command, and data older than the retention period is dropped. | Could (P3) | P3 | 1. One documented command starts it and one stops it. 2. Data lives outside the repo. 3. Retention is configured at the agreed value (see open questions) and the setting is shown in the docs. |

Reviewer link in a PR check is covered by S10 once CI exists; it is not a separate story until Phase 13.

## Success metrics (measurable in this sandbox)

| Metric | Target | How to measure |
|---|---|---|
| Time to find why a build was slow, using only the scan | Under 5 minutes, correct answer | Run a build with a deliberately slowed target. A second person (or a fresh session) answers from the link alone. |
| Share of builds that publish a scan | 100% of default-config builds when the backend is up; 0 failures when it is down | Run 20 mixed builds, count scan URLs. Repeat with the backend stopped and count failed builds. |
| Overhead with backend up | Under 3% wall time on the cold build, under 2 s on a no-op build | Compare timed runs with and without BES. |
| Cache visibility | Hit and miss counts visible for every scan after Phase 10 | Check the second build of the same target. |
| Scan completeness | All required content areas present for a full `bazel test //...` | Walk the parity list above against one scan. |
| Privacy | Zero occurrences of the username or home path in a scan | Search the exported scan data or UI for `$USER` and `$HOME`. |

## Privacy and data governance

- A scan may contain: target names, action mnemonics, timings, test names and results, tool and OS versions, git commit and branch, dependency names and versions, the command line, and truncated test and compiler output.
- A scan must not contain: usernames, absolute home paths, environment variable values (except an allowlist: `CI`, `ENV`), tokens, or credentials. Where the client cannot redact (for example `--client_env`, `--workspace_status_command` output, absolute paths in action output), the backend or a pre-upload step must, or the gap is a documented risk.
- Readers: the repo owner and invited collaborators only. Scan URLs are unguessable but are not a substitute for access control.
- Never public. No scan goes to a public hosted service. A hosted free tier needs the user's decision (see open questions).
- Retention: default 30 days for ordinary scans. Baseline scans are exported and kept. The user confirms the value.
- Deletion: the owner can delete a scan, and wiping the backend removes all data.
- Source code is not uploaded. Test and compiler output are, so secrets printed by tests would appear. The docs say so.

## Reliability requirement

Observability must never become a build dependency.

- A down, slow, or unreachable backend must not fail a build or add more than 5 s of wall time.
- Upload is asynchronous or has a bounded wait (`--bes_timeout`), and a failed upload degrades to a warning.
- A build running offline (no network at all) succeeds.
- Test this on every backend candidate before ranking it. The result gates the choice (see rubric).

## Adoption requirement

"Any build using the platform" means default-on.

- The BES flags sit in the committed `.bazelrc` under `common` or `build`, so every command in the repo gets them.
- A developer opts out in `user.bazelrc` (already imported at the end of `.bazelrc`).
- CI sets a `config:ci` that adds the CI tag and invocation identity.
- Scans must work for sandboxed actions, tests, the `//release` smoke test, and the cold-build baseline.
- A new developer does nothing beyond starting the backend. If starting the backend is manual, the build still works without it (reliability requirement).

## Cost and operations constraints

- One developer machine, macOS arm64. Docker may be used but is not assumed running.
- Prefer free and open-source software. No paid licence for a learning project.
- Minimal always-on services. A backend that starts on demand beats a daemon.
- A hosted service is acceptable only if it meets the privacy requirements and the user approves.
- Disk use is bounded and documented.
- Do not add more than one new stateful service. If the scan backend can also serve as the remote cache (Phase 10), that counts strongly in its favour.

## Weighted evaluation rubric

Use this to compare scan backends in `tlow-hq0`. Score each criterion 0 to 3. Weighted score = sum of (score / 3 x weight), out of 100. Record the evidence for each score (a command run, a screenshot, or a quoted doc line). Say "not verified" instead of guessing.

**Knockouts.** A candidate scoring 0 on criterion 2 (failure isolation) or criterion 3 (privacy and self-host) is rejected whatever its total.

| # | Criterion | Weight | 0 | 1 | 2 | 3 |
|---|---|---|---|---|---|---|
| 1 | Scan content parity with the Develocity list above | 20 | Raw events only, no viewer | Timeline or logs but missing two or more areas | All areas except one (for example no flaky signal, or no cache-miss reason) | All seven areas present |
| 2 | Failure isolation: backend down or offline never fails or slows a build | 15 | Build fails, or waits more than 30 s | Exits 0 but adds 10 to 30 s | Exits 0, adds 5 to 10 s | Exits 0, adds under 5 s, one warning, tested with the backend stopped and with the network off |
| 3 | Privacy and self-host: data stays on the machine, access controllable, redaction possible | 15 | Data goes to a public or third-party service by default | Self-hostable, but no access control or no redaction | Self-hosted with either access control or redaction | Self-hosted, localhost-only or authenticated, and username and path redaction demonstrated |
| 4 | Runs locally on macOS arm64 | 10 | Needs Linux-only or cloud infrastructure | Runs only through emulation, or needs a multi-service cluster | Runs natively or in Docker with manual setup | One binary or one container, starts in under a minute |
| 5 | Can also serve as the remote cache for M2 Phase 10 | 10 | No cache support | Cache via a separate component that needs its own setup | Cache support with limits (no CAS, or paid tier) | Built-in cache and BES in one process, free tier sufficient, verified on arm64 |
| 6 | Licence and cost | 10 | Paid and unavailable for personal use | Paid or trial with a time limit | Free for personal use, with restrictions (non-OSS, or feature-limited) | Open source, no restrictions, no registration |
| 7 | Shareable URL per invocation | 5 | None | URL only works on the host, or needs login for each view | Stable URL, opens with a local session | Stable URL that opens for any permitted reader |
| 8 | Test history and flaky detection | 5 | None | Single-build test results only | Results plus basic flaky labels | Per-test history across builds with flaky flags |
| 9 | Wiring effort: flags in `.bazelrc`, metadata, opt-out, CI config | 5 | Needs wrappers around `bazel` | Needs a patched client or custom plugin | Flags only, some manual steps per user | Flags only, plus works with `user.bazelrc` opt-out and `--build_metadata` unchanged |
| 10 | Ongoing operations cost: upgrades, storage, state | 5 | Needs a database cluster or constant tending | Needs a database or manual cleanup | Single service, manual retention | Single service, built-in retention, trivial wipe |
| | **Total** | **100** | | | | |

Weights: 20 + 15 + 15 + 10 + 10 + 10 + 5 + 5 + 5 + 5 = 100.

Notes for the technical agent:

- Candidates named in the spike: BuildBuddy OSS, Develocity for Bazel, Bazel-native files with a local viewer, other open BES backends. Score each, including ones you reject early (state the knockout).
- "Bazel-native files" will score low on criteria 1, 7, and 8 by construction. Score it honestly; it is the zero-service fallback.
- Verify licence and availability of Develocity for Bazel before scoring. Do not assume.
- Criterion 5 decides `tlow-6ub.6.1.1` together with this spike. State the pairing explicitly in the recommendation.
- A tie within 5 points goes to the option with the higher failure-isolation score, then the lower operations cost.

## Open questions for the user

1. Is a hosted free tier acceptable for this repo? The default assumption is no: self-hosted only. A hosted tier would send command lines, target names, and dependency lists off the machine.
2. Who is the audience of scan links: only you, invited collaborators, or portfolio readers? This sets access control. Portfolio readers would need exported or redacted scans, not live links.
3. What retention do you want? The draft uses 30 days, with exported baselines kept indefinitely.
4. May the backend be an always-on local container, or must it start on demand?
5. Should scan data and the baseline scan be committed or published in the repo (as an exported file, with redaction), so a portfolio reader sees evidence?
6. Is Docker acceptable as a dependency on the dev machine?
7. Should CI scans (Phase 13) go to a different backend from local scans? There is no hosted CI yet, so where would CI scans be sent?
8. Is a 5 s ceiling on added build time acceptable, or do you want it stricter?
