#!/bin/bash
# Negative check (run from the workspace root, not under bazel test: it invokes bazel):
#   * private_consumer passes no visibility -> all 5 targets must be invisible outside the package;
#   * public_consumer passes public -> all 5 visible.
# Uses `bazel build --nobuild` on a throwaway package so nothing is compiled.
set -u
P=tools/build_defs/tests/proto_fixture
T=$P/zz_visibility_probe
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
fail=0
for pkg_vis in private_consumer:closed:fail public_consumer:open:ok; do
  IFS=: read -r pkg nm expect <<<"$pkg_vis"
  for lang in proto cc_grpc java_grpc go_grpc py_grpc; do
    cat >"$T/BUILD.bazel" <<EOB
filegroup(name = "probe", srcs = ["//$P/$pkg:${nm}_$lang"])
EOB
    if bazel build --nobuild "//$T:probe" >/dev/null 2>"$T/err"; then got=ok; else got=fail; fi
    if [[ $got != $expect ]]; then
      echo "FAIL $pkg ${nm}_$lang: expected $expect, got $got"; fail=1
    elif [[ $got == fail ]] && ! grep -q "not visible" "$T/err"; then
      echo "FAIL $pkg ${nm}_$lang: failed but not for visibility"; cat "$T/err"; fail=1
    fi
  done
done
[[ $fail == 0 ]] && echo "PASS: visibility forwarded to all five targets only when passed"
exit $fail
