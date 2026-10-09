"""analytics_go_*: Go wrappers (library, binary, test).

rules_go exports real rules, so these inherit every attribute. Gazelle is configured
(`# gazelle:map_kind`) to generate calls to these macros instead of the raw rules, so
`bazel run //:gazelle` keeps BUILD files on the platform layer.

Policy added: required `team` -> `team:<name>` tag, tests default to size=small.
"""

load("@rules_go//go:def.bzl", "go_binary", "go_library", "go_test")
load(":common.bzl", "TEAM_ATTRS", "present", "team_tags")

def _make_impl(rule):
    def _impl(name, visibility, team, tags, **kwargs):
        rule(name = name, visibility = visibility, tags = team_tags(tags, team), **present(kwargs))

    return _impl

analytics_go_library = macro(
    doc = "go_library with a required `team`.",
    inherit_attrs = go_library,
    attrs = TEAM_ATTRS,
    implementation = _make_impl(go_library),
)

analytics_go_binary = macro(
    doc = "go_binary with a required `team`.",
    inherit_attrs = go_binary,
    attrs = TEAM_ATTRS,
    implementation = _make_impl(go_binary),
)

analytics_go_test = macro(
    doc = "go_test with size=small and a required `team`.",
    inherit_attrs = go_test,
    attrs = TEAM_ATTRS | {
        "size": attr.string(default = "small", configurable = False, values = ["small", "medium", "large", "enormous"]),
    },
    implementation = _make_impl(go_test),
)
