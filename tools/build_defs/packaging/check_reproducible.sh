#!/bin/bash
# Builds an analytics_package target in two clean, separate output bases and
# compares sha256 of <name>.tar and <name>.MANIFEST.json.
# Usage: check_reproducible.sh [//pkg:target]   (default: fixture package)
# Underpins M2 remote-cache hit comparison. Exit 0 = identical, 1 = differ.
set -euo pipefail
TARGET="${1:-//tools/build_defs/tests/package_fixture:pkg_nested}"
cd "$(dirname "$0")/../../.."
WORK="$(mktemp -d)"
trap 'for b in a b; do bazel --output_base="$WORK/$b" shutdown >/dev/null 2>&1 || true; done; chmod -R u+w "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT

sums() {
  local base="$1"
  bazel --output_base="$WORK/$base" build "$TARGET" >&2
  local bin
  bin="$(bazel --output_base="$WORK/$base" cquery --output=files "$TARGET" 2>/dev/null)"
  for f in $bin; do
    shasum -a 256 "$(bazel --output_base="$WORK/$base" info execution_root 2>/dev/null)/$f" | sed "s#  .*/#  #"
  done | sort
}

A="$(sums a)"; B="$(sums b)"
echo "--- build 1"; echo "$A"; echo "--- build 2"; echo "$B"
if [[ "$A" == "$B" && -n "$A" ]]; then echo "REPRODUCIBLE: $TARGET"; else echo "NOT REPRODUCIBLE: $TARGET" >&2; exit 1; fi
