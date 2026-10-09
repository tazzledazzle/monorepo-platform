"""analytics_proto: one proto contract, five generated targets (proto + four gRPC stub languages).

    analytics_proto(name = "query", srcs = ["query.proto"], deps = [":types_proto"], team = "platform",
                    visibility = ["//visibility:public"])

expands to (all carry the `team:<team>` tag and the macro's `visibility`):

  <name>_proto      proto_library
  <name>_cc_grpc    cc_proto_library + cc_grpc_library(grpc_only = True)     (+ private <name>_cc_proto, see _impl)
  <name>_java_grpc  java_proto_library + java_grpc_library                   (+ private <name>_java_proto)
  <name>_go_grpc    go_proto_library with BOTH go_proto and go_grpc_v2
  <name>_py_grpc    py_proto_library + py_grpc_library                       (+ private <name>_py_proto)

Unused languages cost nothing: a target is only analyzed/built when something depends on it,
so a Go-only consumer never compiles the C++ or Java stubs.

`deps` lists other analytics_proto `<x>_proto` labels (the proto_library level). Each language
target is wired to the same dep's stubs for that language: cc/java/py stubs reach a dep's
messages through the transitive proto aspects; Go needs an explicit edge, so `<x>_proto`
maps to `<x>_go_grpc`. Deps must therefore be named `<x>_proto` and come from analytics_proto.

Go import path: defaults to `example.com/three_languages_one_workspace/<package path>`, matching
go.mod, so Go code imports a contract by its repo path. Override with `importpath` only if the
.proto's `go_package` differs.

Repo gotchas encoded here (spike tlow-a6j.10.1): Go needs both compilers (grpc_v2 alone omits
message types); visibility is forwarded to EVERY generated target; Python consumers must also
depend on @pip//protobuf next to grpcio.
"""

load("@grpc-java//:java_grpc_library.bzl", "java_grpc_library")
load("@grpc//bazel:generate_cc.bzl", "generate_cc")
load("@grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_go//proto:def.bzl", "go_proto_library")
load(":common.bzl", "TEAM_ATTRS", "team_tags")

_GO_MODULE = "example.com/three_languages_one_workspace"
_PROTO_SUFFIX = "_proto"

def _sibling(dep, suffix):
    """Maps a dep's `<x>_proto` label to its `<x><suffix>` sibling (same package/repo)."""
    if not dep.name.endswith(_PROTO_SUFFIX):
        fail("analytics_proto deps must be `<x>_proto` targets, got %s" % dep)
    return dep.same_package_label(dep.name[:-len(_PROTO_SUFFIX)] + suffix)

def _impl(name, visibility, team, srcs, deps, importpath, tags, **_kwargs):
    tags = team_tags(tags, team)
    common = {"tags": tags, "visibility": visibility}
    proto = ":" + name + "_proto"

    proto_library(name = name + "_proto", srcs = srcs, deps = deps, **common)

    # C++ (messages via cc_proto_library, services via grpc_only)
    cc_proto_library(name = name + "_cc_proto", deps = [proto], tags = tags, visibility = ["//visibility:private"])
    # This is cc_grpc_library(grpc_only = True) unrolled. The stock macro names its codegen target
    # `_<name>_grpc_codegen`, which a symbolic macro rejects (names must be `<macro>` or `<macro>_*`).
    generate_cc(
        name = name + "_cc_grpc_codegen",
        srcs = [proto],
        plugin = Label("@grpc//src/compiler:grpc_cpp_plugin"),
        well_known_protos = False,
        tags = tags,
        visibility = ["//visibility:private"],
    )
    cc_library(
        name = name + "_cc_grpc",
        srcs = [":" + name + "_cc_grpc_codegen"],
        hdrs = [":" + name + "_cc_grpc_codegen"],
        deps = [":" + name + "_cc_proto", Label("@grpc//:grpc++_codegen_proto")],
        **common
    )

    # Java
    java_proto_library(name = name + "_java_proto", deps = [proto], tags = tags, visibility = ["//visibility:private"])
    java_grpc_library(
        name = name + "_java_grpc",
        srcs = [proto],
        deps = [":" + name + "_java_proto"],
        **common
    )

    # Go: one rule, two compilers; deps are the imported protos' Go targets.
    go_proto_library(
        name = name + "_go_grpc",
        compilers = ["@rules_go//proto:go_proto", "@rules_go//proto:go_grpc_v2"],
        importpath = importpath or (_GO_MODULE + "/" + native.package_name()),
        proto = proto,
        deps = [_sibling(d, "_go_grpc") for d in deps],
        **common
    )

    # Python
    py_proto_library(name = name + "_py_proto", deps = [proto], tags = tags, visibility = ["//visibility:private"])
    py_grpc_library(
        name = name + "_py_grpc",
        srcs = [proto],
        deps = [":" + name + "_py_proto"],
        **common
    )

analytics_proto = macro(
    doc = "proto_library plus cc/java/go/py gRPC stub targets; see the module docstring.",
    attrs = TEAM_ATTRS | {
        "srcs": attr.label_list(allow_files = [".proto"], mandatory = True, configurable = False),
        "deps": attr.label_list(
            configurable = False,
            doc = "`<x>_proto` targets of other analytics_proto macros whose messages these files import.",
        ),
        "importpath": attr.string(
            configurable = False,
            doc = "Go import path. Defaults to example.com/three_languages_one_workspace/<package>.",
        ),
        "tags": attr.string_list(configurable = False),
    },
    implementation = _impl,
)
