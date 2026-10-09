"""analytics_java_*: Java wrappers (library, binary, test).

Like cc.bzl, the attribute lists are explicit: rules_java exports `java_library` etc.
as wrapper functions, which `macro(inherit_attrs = ...)` cannot inherit from. Add any
missing attribute to `_JAVA_ATTRS` rather than bypassing the wrapper.

Policy added on top of the wrapped rule:
  * `-Xlint:all -Werror` for first-party sources (generated gRPC stubs are separate
    targets, so they are not subject to it);
  * tests get JUnit 4 and a derived `test_class` (see jvm.bzl);
  * `team` tag, `size = "small"` (see common.bzl).
"""

load("@rules_java//java:java_binary.bzl", "java_binary")
load("@rules_java//java:java_library.bzl", "java_library")
load("@rules_java//java:java_test.bzl", "java_test")
load(":common.bzl", "TEAM_ATTRS", "TEST_ATTRS", "present", "prepend", "team_tags", "test_attrs")
load(":jvm.bzl", "derive_test_class")

_JUNIT = Label("@maven//:junit_junit")
_JAVACOPTS = ["-Xlint:all", "-Werror"]

_JAVA_ATTRS = {
    "deps": attr.label_list(),
    "runtime_deps": attr.label_list(),
    "resources": attr.label_list(allow_files = True),
    "resource_strip_prefix": attr.string(),
    "data": attr.label_list(allow_files = True),
    "javacopts": attr.string_list(),
}

_JVM_RUN_ATTRS = {"jvm_flags": attr.string_list()}

# `srcs` is non-configurable on tests so the macro can inspect it to derive test_class.
_SRCS = {"srcs": attr.label_list(allow_files = True)}
_SRCS_INSPECTABLE = {"srcs": attr.label_list(allow_files = True, configurable = False)}

def _common(kwargs, team, javacopts):
    kwargs = present(kwargs)
    kwargs["tags"] = team_tags(kwargs.pop("tags", None), team)
    kwargs["javacopts"] = prepend(_JAVACOPTS, javacopts)
    return kwargs

def _library_impl(name, visibility, team, javacopts, **kwargs):
    java_library(name = name, visibility = visibility, **_common(kwargs, team, javacopts))

def _binary_impl(name, visibility, team, javacopts, **kwargs):
    java_binary(name = name, visibility = visibility, **_common(kwargs, team, javacopts))

def _test_impl(name, visibility, team, javacopts, deps, srcs, test_class, size, timeout, args, env, flaky, shard_count, **kwargs):
    kwargs.update(test_attrs(size, timeout, args, env, flaky, shard_count))
    kwargs["deps"] = prepend([_JUNIT], deps)
    kwargs["srcs"] = srcs
    kwargs["test_class"] = derive_test_class(test_class = test_class, name = name, srcs = srcs)
    java_test(name = name, visibility = visibility, **_common(kwargs, team, javacopts))

analytics_java_library = macro(
    doc = "java_library with -Xlint:all -Werror and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _JAVA_ATTRS | _SRCS | {
        "exports": attr.label_list(),
        "neverlink": attr.bool(),
    },
    implementation = _library_impl,
)

analytics_java_binary = macro(
    doc = "java_binary with -Xlint:all -Werror and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _JAVA_ATTRS | _JVM_RUN_ATTRS | _SRCS | {
        "main_class": attr.string(),
    },
    implementation = _binary_impl,
)

analytics_java_test = macro(
    doc = "java_test with -Xlint:all -Werror, JUnit 4, derived test_class, size=small and a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | TEST_ATTRS | _JAVA_ATTRS | _JVM_RUN_ATTRS | _SRCS_INSPECTABLE | {
        "test_class": attr.string(configurable = False),
    },
    implementation = _test_impl,
)
