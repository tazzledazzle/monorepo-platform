"""analytics_go_*: Go wrappers (library, binary, test).

rules_go exports real rules for go_library and go_test, so those inherit every attribute.
`go_binary` is a wrapper *function* (which `inherit_attrs` cannot take), so analytics_go_binary
lists its attributes explicitly in `_GO_BINARY_ATTRS`; add any that are missing there. Gazelle is configured
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

_GO_BINARY_ATTRS = {
    "srcs": attr.label_list(allow_files = [".go", ".s", ".h", ".c"]),
    "deps": attr.label_list(),
    "embed": attr.label_list(),
    "data": attr.label_list(allow_files = True),
    "importpath": attr.string(),
    "gc_linkopts": attr.string_list(),
    "x_defs": attr.string_dict(),
    "pure": attr.string(values = ["on", "off", "auto"], default = "auto"),
    "static": attr.string(values = ["on", "off", "auto"], default = "auto"),
}

analytics_go_binary = macro(
    doc = "go_binary with a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _GO_BINARY_ATTRS,
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
