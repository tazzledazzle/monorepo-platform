"""analytics_grpc_service: library + binary + test for a gRPC server, in C++ or Kotlin.

    analytics_grpc_service(
        name = "gateway",
        lang = "kt",
        team = "server",
        srcs = glob(["src/main/kotlin/**/*.kt"]),
        test_srcs = glob(["src/test/kotlin/**/*.kt"]),
        protos = ["//proto/analytics/v1:query"],
        main = "analytics.gateway.MainKt",
    )

expands to `:gateway_lib`, `:gateway` (binary) and `:gateway_test`, all built with the
analytics_cc_* / analytics_kt_* macros (so -Werror, the `team:` tag and the test defaults
come along).

`protos` entries are the bare `<x>` name of an analytics_proto (its `<x>_proto` sibling
family); the macro picks `<x>_cc_grpc` for cc and `<x>_java_grpc` for kt (Kotlin uses the
Java stubs). The gRPC runtime is added here so teams do not repeat it: @grpc//:grpc++ for
cc, and grpc-java api/stub/protobuf/netty/inprocess plus protobuf-java for kt.

`main` means the entry point:
  * kt: the fully qualified main class (e.g. "analytics.gateway.MainKt");
  * cc: the source file holding main(), relative to the package (e.g. "main.cc"). It is
    compiled only into the binary, so it is dropped from `srcs` if a glob picked it up.

Only cc and kt are supported (the only server languages). Adding one is the UC-03 exercise:
extend _LANGS and add a branch in _impl.
"""

load(":cc.bzl", "analytics_cc_binary", "analytics_cc_library", "analytics_cc_test")
load(":common.bzl", "TEAM_ATTRS")
load(":kt.bzl", "analytics_kt_binary", "analytics_kt_library", "analytics_kt_test")

_LANGS = ["cc", "kt"]

_GRPCPP = Label("@grpc//:grpc++")

# gRPC 1.84's public headers use absl::MutexLock(ptr), deprecated by the abseil we resolve, and
# templates in them re-instantiate it in first-party code. Keep -Werror for our own warnings but
# do not let third-party deprecation notices fail the build. Revisit when grpc/abseil agree.
_GRPC_HEADER_NOISE = select({
    "@rules_cc//cc/compiler:clang": ["-Wno-error=deprecated-declarations"],
    "@rules_cc//cc/compiler:gcc": ["-Wno-error=deprecated-declarations"],
    "//conditions:default": [],
})

# What a Kotlin gRPC server needs on its classpath beyond the generated stubs.
_GRPC_JAVA_RUNTIME = [
    Label("@grpc-java//api"),
    Label("@grpc-java//stub"),
    Label("@grpc-java//protobuf"),
    Label("@grpc-java//netty"),
    Label("@grpc-java//inprocess"),
    Label("@maven//:com_google_protobuf_protobuf_java"),
]

# Sibling suffixes of an analytics_proto: a `protos` entry must not already carry one.
_PROTO_SUFFIXES = ["_proto", "_cc_proto", "_cc_grpc", "_java_proto", "_java_grpc", "_go_grpc", "_py_proto", "_py_grpc"]

def _stub_for(name, proto, lang):
    for suffix in _PROTO_SUFFIXES:
        if proto.name.endswith(suffix):
            fail("analytics_grpc_service %s: protos entries are the analytics_proto name `<x>` " % name +
                 "(e.g. \"//proto/analytics/v1:query\"), not a generated target; got %s" % proto)
    return proto.same_package_label(proto.name + ("_cc_grpc" if lang == "cc" else "_java_grpc"))

def _impl(name, visibility, lang, team, srcs, test_srcs, protos, deps, test_deps, main, test_class, size, tags, **_kwargs):
    stubs = [_stub_for(name, p, lang) for p in protos]
    lib = ":" + name + "_lib"
    common = {"team": team, "tags": tags}

    if lang == "cc":
        main_src = None
        if main:
            main_src = native.package_relative_label(main)
            srcs = [s for s in srcs if s != main_src]
        if not main_src:
            fail("analytics_grpc_service %s: lang = \"cc\" needs main = \"<file with main()>\"" % name)
        analytics_cc_library(
            name = name + "_lib",
            srcs = srcs,
            deps = stubs + [_GRPCPP] + deps,
            copts = _GRPC_HEADER_NOISE,
            visibility = visibility,
            **common
        )
        analytics_cc_binary(
            name = name,
            srcs = [main_src],
            deps = [lib],
            copts = _GRPC_HEADER_NOISE,
            visibility = visibility,
            **common
        )
        analytics_cc_test(
            name = name + "_test",
            srcs = test_srcs,
            deps = [lib] + test_deps,
            copts = _GRPC_HEADER_NOISE,
            size = size,
            visibility = ["//visibility:private"],
            **common
        )
    else:
        if not main:
            fail("analytics_grpc_service %s: lang = \"kt\" needs main = \"<fully qualified main class>\"" % name)
        analytics_kt_library(
            name = name + "_lib",
            srcs = srcs,
            deps = stubs + _GRPC_JAVA_RUNTIME + deps,
            visibility = visibility,
            **common
        )
        analytics_kt_binary(
            name = name,
            main_class = main,
            runtime_deps = [lib],
            visibility = visibility,
            **common
        )
        analytics_kt_test(
            name = name + "_test",
            srcs = test_srcs,
            deps = [lib] + stubs + _GRPC_JAVA_RUNTIME + test_deps,
            test_class = test_class,
            size = size,
            visibility = ["//visibility:private"],
            **common
        )

analytics_grpc_service = macro(
    doc = "Library, binary and test for a gRPC server in cc or kt; see the module docstring.",
    attrs = TEAM_ATTRS | {
        "lang": attr.string(mandatory = True, configurable = False, values = _LANGS, doc = "'cc' or 'kt'."),
        "srcs": attr.label_list(allow_files = True, configurable = False, doc = "Library sources."),
        "test_srcs": attr.label_list(allow_files = True, configurable = False, doc = "Unit-test sources."),
        "protos": attr.label_list(configurable = False, doc = "analytics_proto names `<x>`; mapped to <x>_cc_grpc / <x>_java_grpc."),
        "deps": attr.label_list(doc = "Extra library deps."),
        "test_deps": attr.label_list(doc = "Extra test-only deps."),
        "main": attr.string(configurable = False, doc = "kt: main class. cc: file containing main()."),
        "test_class": attr.string(configurable = False, doc = "kt only: override the derived JUnit class."),
        "size": attr.string(default = "small", configurable = False, values = ["small", "medium", "large", "enormous"]),
        "tags": attr.string_list(configurable = False),
    },
    implementation = _impl,
)
