#!/bin/bash
# Exercises the make_tarball tool directly: determinism and duplicate-path failure.
set -euo pipefail
TOOL="$1"
T="${TEST_TMPDIR:-$(mktemp -d)}"
printf 'a' > "$T/a.txt"; printf 'b' > "$T/b.txt"; chmod +x "$T/b.txt"
spec() { # entries in the given order
  : > "$T/spec"
  for e in "$@"; do
    IFS='|' read -r path src label <<<"$e"
    printf '{"path":"%s","src":"%s","label":"%s"}\n' "$path" "$T/$src" "$label" >> "$T/spec"
  done
}
run() { "$TOOL" --spec="$T/spec" --out_tar="$T/$1.tar" --out_manifest="$T/$1.json"; }

spec "z/b|b.txt|//x:b" "a/a|a.txt|//x:a"
run one; sleep 1; touch "$T/a.txt"; run two
cmp "$T/one.tar" "$T/two.tar" || { echo "tar not reproducible"; exit 1; }
cmp "$T/one.json" "$T/two.json" || { echo "manifest not reproducible"; exit 1; }
# Input order must not matter.
spec "a/a|a.txt|//x:a" "z/b|b.txt|//x:b"; run three
cmp "$T/one.tar" "$T/three.tar" || { echo "order-dependent tar"; exit 1; }
grep -q '"label": "//x:a"' "$T/one.json" && grep -q '"sha256"' "$T/one.json" || { echo "bad manifest"; exit 1; }

# Duplicate install path from different files must fail naming both labels.
spec "d/f|a.txt|//x:first" "d/f|b.txt|//x:second"
if run dup 2> "$T/err"; then echo "duplicate did not fail"; exit 1; fi
grep -q '//x:first' "$T/err" && grep -q '//x:second' "$T/err" || { cat "$T/err"; echo "labels missing"; exit 1; }
echo PASS
