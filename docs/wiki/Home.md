# three-languages-one-workspace

A learning sandbox that rebuilds a small unified Bazel monorepo platform, in the spirit of Tableau's "Blitz" migration: five languages (C++, Java, Kotlin, Go, Python), one build graph, and a platform layer that product teams use instead of raw `rules_*`.

## Status in one paragraph

Milestone M1 (phases 1-7) is built and verified on macOS arm64. `tools/build_defs` provides 15 wrapper macros, `analytics_proto`, `analytics_grpc_service`, and a reproducible `analytics_package` rule. They build a C++ engine, a Java catalog, a Kotlin gateway, a Go ingest agent, and a Python report CLI over one protobuf schema. `bazel test //...` passes 34 tests, and ten negative cases pass. M2 (hermeticity, sanitizers, remote cache), M3 (CI, governance), and M4 (platforms, cross-compilation) are not started. No CI exists. See [Roadmap](Roadmap).

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
