"""analytics_py_*: Python wrappers (library, binary, test).

rules_python exports `py_library`/`py_binary`/`py_test` as wrapper functions, which
`macro(inherit_attrs = ...)` cannot inherit from, so the attribute lists are explicit
(same trade-off as cc.bzl and java.bzl). Add any missing attribute to `_PY_ATTRS`.

Policy added on top of the wrapped rule:
  * every test runs under pytest through the shared shim `pytest_main.py`, which receives
    the test files as arguments; teams never write a `main`;
  * `team` tag, `size = "small"` (see common.bzl).
"""

load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_library.bzl", "py_library")
load("@rules_python//python:py_test.bzl", "py_test")
load(":common.bzl", "TEAM_ATTRS", "TEST_ATTRS", "present", "team_tags", "test_attrs")

_PYTEST = Label("@pip//pytest")
_SHIM = Label(":pytest_main.py")

_PY_ATTRS = {
    "srcs": attr.label_list(allow_files = [".py"]),
    "deps": attr.label_list(),
    "data": attr.label_list(allow_files = True),
    "imports": attr.string_list(),
}

def _common(kwargs, team):
    kwargs = present(kwargs)
    kwargs["tags"] = team_tags(kwargs.pop("tags", None), team)
    return kwargs

def _library_impl(name, visibility, team, **kwargs):
    py_library(name = name, visibility = visibility, **_common(kwargs, team))

def _binary_impl(name, visibility, team, **kwargs):
    py_binary(name = name, visibility = visibility, **_common(kwargs, team))

def _test_impl(name, visibility, team, srcs, deps, size, timeout, args, env, flaky, shard_count, **kwargs):
    kwargs.update(test_attrs(size, timeout, args, env, flaky, shard_count))
    kwargs["srcs"] = srcs + [_SHIM]
    kwargs["main"] = _SHIM
    kwargs["deps"] = [_PYTEST] + deps
    kwargs["args"] = ["$(rootpath %s)" % s for s in srcs] + (args or [])
    py_test(name = name, visibility = visibility, **_common(kwargs, team))

analytics_py_library = macro(
    doc = "py_library with a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _PY_ATTRS,
    implementation = _library_impl,
)

analytics_py_binary = macro(
    doc = "py_binary with a required `team`.",
    inherit_attrs = "common",
    attrs = TEAM_ATTRS | _PY_ATTRS | {"main": attr.label(allow_single_file = [".py"])},
    implementation = _binary_impl,
)

analytics_py_test = macro(
    doc = "py_test run by pytest through a shared shim, size=small, with a required `team`.",
    inherit_attrs = "common",
    # `srcs` is non-configurable so the macro can turn each file into a pytest argument.
    attrs = TEAM_ATTRS | TEST_ATTRS | _PY_ATTRS | {
        "srcs": attr.label_list(allow_files = [".py"], configurable = False),
    },
    implementation = _test_impl,
)
