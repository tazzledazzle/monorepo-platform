"""analytics_kt_*: Kotlin wrappers (library, test).

Unlike cc/java/py, rules_kotlin exports real rules, so these macros genuinely
`inherit_attrs` from them and every attribute of kt_jvm_library/kt_jvm_test is accepted.

Policy added on top of the wrapped rule:
  * Kotlin warnings are errors. The `kotlinc_opts` attribute is removed from the macro's
    API so a team cannot opt out by passing its own options; the macro supplies a private
    `<name>_kotlinc_opts` target instead. Only first-party sources are affected: generated
    gRPC stubs are Java and live in separate targets;
  * tests get JUnit 4 and a derived `test_class` (see jvm.bzl);
  * `team` tag, `size = "small"` (see common.bzl).
"""

load("@rules_kotlin//kotlin:core.bzl", "kt_kotlinc_options")
load("@rules_kotlin//kotlin:jvm.bzl", "kt_jvm_library", "kt_jvm_test")
load(":common.bzl", "TEAM_ATTRS", "present", "team_tags")
load(":jvm.bzl", "derive_test_class")

_JUNIT = Label("@maven//:junit_junit")

def _strict_options(name, visibility):
    opts = name + "_kotlinc_opts"
    kt_kotlinc_options(name = opts, warn = "error", visibility = visibility)
    return ":" + opts

def _library_impl(name, visibility, team, tags, **kwargs):
    kt_jvm_library(
        name = name,
        visibility = visibility,
        tags = team_tags(tags, team),
        kotlinc_opts = _strict_options(name, visibility),
        **present(kwargs)
    )

def _test_impl(name, visibility, team, tags, deps, srcs, test_class, **kwargs):
    kt_jvm_test(
        name = name,
        visibility = visibility,
        tags = team_tags(tags, team),
        kotlinc_opts = _strict_options(name, visibility),
        deps = [_JUNIT] + deps,
        srcs = srcs,
        test_class = derive_test_class(test_class = test_class, name = name, srcs = srcs),
        **present(kwargs)
    )

analytics_kt_library = macro(
    doc = "kt_jvm_library with warnings-as-errors and a required `team`.",
    inherit_attrs = kt_jvm_library,
    attrs = TEAM_ATTRS | {"kotlinc_opts": None},
    implementation = _library_impl,
)

analytics_kt_test = macro(
    doc = "kt_jvm_test with warnings-as-errors, JUnit 4, derived test_class, size=small and a required `team`.",
    inherit_attrs = kt_jvm_test,
    attrs = TEAM_ATTRS | {
        "kotlinc_opts": None,
        # Non-configurable so the macro can inspect it to derive test_class.
        "srcs": attr.label_list(allow_files = [".kt", ".java"], configurable = False),
        "size": attr.string(default = "small", configurable = False, values = ["small", "medium", "large", "enormous"]),
        "test_class": attr.string(configurable = False),
        "deps": attr.label_list(providers = [[JavaInfo]]),
    },
    implementation = _test_impl,
)
