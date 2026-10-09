"""Test-only symbolic macro mirroring legacy_cc_library: it creates `<name>` and an
inner `<name>_hdrs` helper. The helper gets no visibility, so it stays private to the macro."""

def _impl(name, visibility, hdrs):
    native.filegroup(name = name, srcs = hdrs, visibility = visibility)
    native.filegroup(name = name + "_hdrs", srcs = hdrs)

modern_with_helper = macro(
    attrs = {"hdrs": attr.label_list(allow_files = True, configurable = False)},
    implementation = _impl,
)
