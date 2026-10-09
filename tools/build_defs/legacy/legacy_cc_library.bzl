"""COUNTER-EXAMPLE. Do not copy this style. Use the symbolic macros in //tools/build_defs.

`legacy_cc_library` is a classic (non-symbolic) macro: a plain Starlark function that
instantiates rules when a BUILD file is loaded. It wraps cc_library and "helpfully"
also creates a header filegroup, `<name>_hdrs`.

What goes wrong with this style, and how the symbolic macros avoid it:

  1. Undeclared public helper. `<name>_hdrs` is created with public visibility and is
     not part of the macro's documented interface. Any package can depend on it, and
     once someone does, the helper becomes de-facto API that cannot be removed or
     renamed without breaking them. In a symbolic macro (`macro(...)`), targets the
     implementation creates are private to the macro by default; only the main
     target `name` gets the caller's visibility, so this leak cannot happen by
     accident (see //tools/build_defs/cc.bzl).

  2. No typed attributes. `**kwargs` accepts anything: a typo such as `hdr = [...]`
     or a missing `team` is only caught (if at all) deep inside cc_library. Symbolic
     macros declare `attrs`, so unknown or mistyped attributes and missing mandatory
     ones (`team`) fail at load time with a clear message.

  3. No visibility enforcement on inner targets. Classic macros are invisible to the
     visibility system: nothing distinguishes "what the macro exposes" from "what it
     happens to create". There is also no `inherit_attrs`, so common attributes must
     be forwarded by hand and drift out of date.

The negative test in //tests/negative demonstrates (1): a target in another package
depends on //tools/build_defs/legacy/sample:legacy_lib_hdrs and builds successfully.
"""

load("@rules_cc//cc:cc_library.bzl", "cc_library")

def legacy_cc_library(name, **kwargs):
    """cc_library plus a quietly-created public `<name>_hdrs` filegroup (the leak)."""
    hdrs = kwargs.get("hdrs", [])
    cc_library(name = name, **kwargs)

    # The leak: a second, public, undocumented target.
    native.filegroup(
        name = name + "_hdrs",
        srcs = hdrs,
        visibility = ["//visibility:public"],
    )
