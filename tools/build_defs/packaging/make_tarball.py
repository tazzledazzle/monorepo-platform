"""Builds a reproducible tar plus MANIFEST.json for analytics_package.

Usage: make_tarball --spec=<file> --out_tar=<tar> --out_manifest=<json>

The spec file holds one JSON object per line:
  {"path": "<install path>", "src": "<file on disk>", "label": "<source label>"}

Reproducibility: entries are sorted by install path, mtime is 0, uid/gid are 0,
user/group names are empty, dangling symlink artifacts are skipped, modes are normalised to 0644/0755, and the plain
GNU tar format is used (no gzip header, no PAX timestamps).
"""

import argparse
import hashlib
import json
import os
import sys
import tarfile


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _load_entries(spec_path):
    """Returns {install_path: entry}. Exits nonzero on conflicting paths."""
    entries = {}
    with open(spec_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            e = json.loads(line)
            path = os.path.normpath(e["path"]).lstrip("/")
            e["path"] = path
            prev = entries.get(path)
            if prev is None:
                entries[path] = e
            elif prev["src"] != e["src"]:
                # Same install path from two different files: a real conflict.
                sys.stderr.write(
                    "ERROR: duplicate install path '%s' provided by both %s and %s\n"
                    % (path, prev["label"], e["label"])
                )
                sys.exit(1)
            # Identical path and identical source file is the same entry reached twice.
    return entries


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--spec", required=True)
    p.add_argument("--out_tar", required=True)
    p.add_argument("--out_manifest", required=True)
    args = p.parse_args()

    entries = _load_entries(args.spec)
    manifest = []
    with tarfile.open(args.out_tar, "w", format=tarfile.GNU_FORMAT) as tar:
        for path in sorted(entries):
            e = entries[path]
            src = e["src"]
            if os.path.islink(src) and not os.path.exists(src):
                # Unresolved symlink artifacts (e.g. rules_python's _x.venv/bin/python3)
                # only make sense inside a runfiles tree; their target is not packaged.
                sys.stderr.write("WARNING: skipping dangling symlink %s (from %s)\n" % (src, e["label"]))
                continue
            if not os.path.isfile(src):
                sys.stderr.write("ERROR: %s (from %s) is not a regular file\n" % (src, e["label"]))
                sys.exit(1)
            info = tarfile.TarInfo(path)
            info.size = os.path.getsize(src)
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o755 if os.stat(src).st_mode & 0o111 else 0o644
            with open(src, "rb") as f:
                tar.addfile(info, f)
            manifest.append({"path": path, "sha256": _sha256(src), "label": e["label"]})

    with open(args.out_manifest, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")


if __name__ == "__main__":
    main()
