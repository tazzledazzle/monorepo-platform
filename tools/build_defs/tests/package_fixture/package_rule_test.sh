#!/bin/bash
# Verifies analytics_package output for the throwaway fixtures.
set -euo pipefail
D="$(dirname "$0")"
fail() { echo "FAIL: $*" >&2; exit 1; }

BASE_TAR="$D/pkg_base.tar"; NEST_TAR="$D/pkg_nested.tar"
[[ -f $BASE_TAR && -f $NEST_TAR ]] || fail "tars missing in $D"

base_list="$(tar tf "$BASE_TAR")"
nest_list="$(tar tf "$NEST_TAR")"

grep -q '^engine/.*hello_cc$' <<<"$base_list" || fail "cc binary missing"
grep -q '^cli/.*hello_py$' <<<"$base_list" || fail "py binary missing"
grep -q '^cli/.*hello\.py$' <<<"$base_list" || fail "py runfiles (source) missing"
[[ "$base_list" == "$(echo "$base_list" | LC_ALL=C sort)" ]] || fail "entries not sorted"

# Nesting merges the deps provider: everything in base is also in nested, plus data/.
missing="$(comm -23 <(LC_ALL=C sort <<<"$base_list") <(LC_ALL=C sort <<<"$nest_list"))"
[[ -z $missing ]] || fail "nested missing: $missing"
grep -q '^data/.*gen\.txt$' <<<"$nest_list" || fail "genrule output missing in nested"

# Manifest: path, sha256, label for every file; checksums match tar content.
python3 - "$D/pkg_nested.MANIFEST.json" "$NEST_TAR" <<'PY'
import hashlib, json, sys, tarfile
m = json.load(open(sys.argv[1]))
t = tarfile.open(sys.argv[2])
for ti in t.getmembers():
    assert (ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname) == (0, 0, 0, "", ""), ti.name
assert [e["path"] for e in m] == sorted(e["path"] for e in m)
assert sorted(t.getnames()) == [e["path"] for e in m], "manifest/tar path mismatch"
for e in m:
    assert set(e) == {"path", "sha256", "label"}, e
    assert e["label"].startswith("@@") or e["label"].startswith("//"), e
    assert hashlib.sha256(t.extractfile(e["path"]).read()).hexdigest() == e["sha256"], e
print("manifest ok:", len(m), "entries")
PY
echo PASS
