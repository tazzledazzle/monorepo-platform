# three-languages-one-workspace

A learning sandbox that rebuilds a small unified Bazel monorepo platform, in the spirit of Tableau's "Blitz" migration: five languages (C++, Java, Kotlin, Go, Python), one build graph, and a platform layer that product teams use instead of raw `rules_*`.

## Status in one paragraph

Phases 1 and 2 of 17 are done: all dependencies are pinned, gRPC code generation was proven for five languages in a spike, and `tools/build_defs` provides 15 wrapper macros plus a reproducible `analytics_package` rule. The proto schema and the `analytics_proto` macro are in progress. The product services, release tests, sanitizers, remote cache, CI, and cross-compilation are planned, not built. See [Roadmap](Roadmap).

## Where to start

| If you want to | Read |
|---|---|
| Run it | [Getting Started](Getting-Started) |
| Understand the layout and rules | [Architecture](Architecture) |
| Look up a macro | [Platform Macros](Platform-Macros) |
| Understand the package rule | [Packaging Rule](Packaging-Rule) |
| Write a BUILD file | [Adding a Target](Adding-a-Target) |
| See what is done and what is next | [Roadmap](Roadmap) |
| Learn from the bugs I hit | [Findings and Gotchas](Findings-and-Gotchas) |
| Decode a term | [Glossary](Glossary) |

License: not yet chosen.
