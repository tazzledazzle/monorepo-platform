# Planning in beads (GSD parity)

This project plans in beads instead of `.planning/`. Every GSD concept has a beads equivalent.
(A `.planning/` directory is deliberately not used: `~/dev/.planning` belongs to an unrelated project, and
GSD tooling resolves to the nearest ancestor `.planning/`.)

| GSD | Beads |
|---|---|
| `PROJECT.md` | Epic `tlow-049` (what, core value, constraints) |
| Milestone | Milestone epics `tlow-a6j` (M1), `tlow-6ub` (M2), `tlow-6p3` (M3), `tlow-yn7` (M4) |
| Phase (goal + success criteria) | `Phase N:` epic under its milestone; goal in the description, success criteria in the acceptance field |
| Phase scope (plans) | Theme epics → stories → tasks, all labelled `phase:N` |
| `/gsd-verify-work` (phase gate) | Close the phase epic with a comment citing evidence. The next phase's theme epics depend on it. |
| `/gsd-progress`, `STATE.md` | `bd ready`, `bd stats`, `bd list --label phase:N` |
| Roadmap | `bd show <milestone-epic>`; phases are numbered continuously across milestones |
| Spike | `SPIKE:` task, label `spike` |

## Phases

| # | Phase | Milestone | Epic | Needs |
|---|---|---|---|---|
| 1 | Bootstrap and codegen spike | M1 | `tlow-a6j.16` | none |
| 2 | Platform macro and packaging layer | M1 | `tlow-a6j.18` | 1 |
| 3 | Proto contracts and composite macros | M1 | `tlow-a6j.17` | 2 |
| 4 | C++ query engine | M1 | `tlow-a6j.19` | 3 |
| 5 | JVM services: catalog and gateway | M1 | `tlow-a6j.20` | 3 |
| 6 | Clients: ingest agent and report CLI | M1 | `tlow-a6j.21` | 3 |
| 7 | Guardrails, release verification and docs | M1 | `tlow-a6j.22` | 4, 5, 6 |
| 8 | Hermetic builds | M2 | `tlow-6ub.7` | 7 |
| 9 | Sanitizer-clean engine | M2 | `tlow-6ub.9` | 8 |
| 10 | Local remote cache | M2 | `tlow-6ub.8` | 9 |
| 11 | Dependency pinning and conflict detection | M3 | `tlow-6p3.9` | 7 |
| 12 | Affected-target detection | M3 | `tlow-6p3.10` | 7 |
| 13 | CI pipeline | M3 | `tlow-6p3.11` | 11, 12 |
| 14 | Governance: lint, ownership, proto compatibility | M3 | `tlow-6p3.12` | 11, 12 |
| 15 | Platforms and toolchains | M4 | `tlow-yn7.1` | 7 |
| 16 | Cross-compile Linux from macOS | M4 | `tlow-yn7.2` | 15 |
| 17 | Legacy CMake strangler shim | M4 | `tlow-yn7.3` | 15 |

Phases 15 to 17 are labelled `needs-planning`: they have goals but no stories yet.

## Working a phase

1. `bd ready` shows only unblocked work. Theme epics of a phase stay hidden until the previous phase is closed.
2. Claim a task (`bd update <id> --claim`), do it, close it with the evidence.
3. When every theme epic in the phase is closed, check the phase epic's acceptance criteria, add a
   `GATE PASSED` comment naming the evidence, and close it. That unblocks the next phase.
4. If acceptance criteria turn out to belong to a later phase, amend them and say so in the issue
   (as was done for analysistests, now in Phase 7 `tlow-a6j.14.1`).

## Adding a phase

```sh
bd create "Phase N: <name>" -t epic --parent <milestone-epic> -l phase,phase:N,m<k> \
  -d "GOAL: ..." --acceptance "1. ... 2. ..."
bd dep add <phase-epic> <prerequisite-phase-epic>
# then for each theme epic in it: label its subtree phase:N and `bd dep add <theme-epic> <prerequisite-phase-epic>`
```
