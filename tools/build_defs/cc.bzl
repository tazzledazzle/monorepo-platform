"""analytics_cc_*: C++ wrappers (library, binary, test).

Why the attribute lists below are explicit instead of `inherit_attrs = cc_library`:
rules_cc exports `cc_library`/`cc_binary`/`cc_test` as plain wrapper *functions*,
not rules or symbolic macros, and `macro(inherit_attrs = ...)` only accepts a rule
or macro symbol. Only the "common" attributes (tags, testonly, features, ...) are
inherited; the C++-specific ones are listed here. If a target needs an attribute
that is missing, add it to `_CC_ATTRS` rather than bypassing the wrapper.

Policy added on top of the wrapped rule:
  * warnings as errors, chosen per compiler via select() so a future MSVC/GCC
    toolchain (M4) works without touching this file;
  * tests link @googletest//:gtest_main;
  * `team` tag, `size = "small"` (see common.bzl).
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load(":common.bzl", "TEAM_ATTRS", "TEST_ATTRS", "present", "prepend", "team_tags", "test_attrs")

_GTEST_MAIN = Label("@googletest//:gtest_main")

# Compiler-specific, never platform-specific: nothing here mentions macOS or Linux.
_WARNINGS = select({
    "@rules_cc//cc/compiler:clang": ["-Wall", "-Wextra", "-Werror"],
    "@rules_cc//cc/compiler:gcc": ["-Wall", "-Wextra", "-Werror"],
    "@rules_cc//cc/compiler:msvc-cl": ["/W4", "/WX"],
    "//conditions:default": [],
})

_CC_ATTRS = {
    "srcs": attr.label_list(allow_files = True),
    "deps": attr.label_list(),
    "data": attr.label_list(allow_files = True),
    "copts": attr.string_list(),
    "defines": attr.string_list(),
    "local_defines": attr.string_list(),
    "includes": attr.string_list(),
    "linkopts": attr.string_list(),
    "linkstatic": attr.bool(),
}

_CC_LIBRARY_ATTRS = _CC_ATTRS | {
    "hdrs": attr.label_list(allow_files = True),
    "textual_hdrs": attr.label_list(allow_files = True),
    "include_prefix": attr.string(),
    "strip_include_prefix": attr.string(),
    "alwayslink": attr.bool(),
}

def _common(kwargs, team, copts):
    kwargs = present(kwargs)
    kwargs["tags"] = team_tags(kwargs.pop("tags", None), team)
    kwargs["copts"] = prepend(_WARNINGS, copts)
    return kwargs

def _library_impl(name, visibility, team, copts, **kwargs):
    cc_library(name = name, visibility = visibility, **_common(kwargs, team, copts))

def _binary_impl(name, visibility, team, copts, **kwargs):
    cc_binary(name = name, visibility = visibility, **_common(kwargs, team, copts))

def _test_impl(name, visibility, team, copts, deps, size, timeout, args, env, flaky, shard_count, **kwargs):
    kwargs.update(test_attrs(size, timeout, args, env, flaky, shard_count))
    kwargs["deps"] = prepend([_GTEST_MAIN], deps)
    cc_test(name = name, visibility = visibility, **_common(kwargs, team, copts))

analytics_cc_library = macro(
    doc = "cc_library with warnings-as-errors and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _CC_LIBRARY_ATTRS,
    implementation = _library_impl,
)

analytics_cc_binary = macro(
    doc = "cc_binary with warnings-as-errors and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _CC_ATTRS,
    implementation = _binary_impl,
)

analytics_cc_test = macro(
    doc = "cc_test with warnings-as-errors, gtest_main linked in, size=small and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | TEST_ATTRS | _CC_ATTRS,
    implementation = _test_impl,
)
