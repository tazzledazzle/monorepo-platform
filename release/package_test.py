"""Checks the built //release:analytics package: tar vs MANIFEST.json vs unpacked files.

Usage (via Bazel): package_test.py <analytics.tar> <analytics.MANIFEST.json> (any order)

What is verified for the real package:
  * every MANIFEST.json entry exists in the tar and in the unpacked tree, with a matching sha256;
  * every tar member is listed in the manifest (no unlisted payload);
  * the expected executables / artefacts of the four components are present;
  * the self-contained components actually start when unpacked (engine prints LISTENING, agent
    prints its usage); the gateway jar is a valid jar naming the right Main-Class.

Known limitation, documented rather than asserted: the Python CLI is packaged with its runfiles
tree contents but not as a `cli.runfiles/` layout, so its launcher cannot run when unpacked
("Cannot find .runfiles directory"). See tools/build_defs/package.bzl.

The negative unit tests below build tiny synthetic packages and tamper with them, proving the
checker actually fails on a changed file, a missing executable, and an unlisted tar member.
"""

import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
import zipfile

# (path in tar, must be executable)
EXPECTED = [
    ("engine/engine/service/engine", True),
    ("agent/ingest/agent/main/agent_/agent", True),
    ("gateway/release/gateway_deploy.jar", False),
    ("cli/reports/cli/cli", True),
    ("cli/reports/cli/main.py", False),
    ("cli/reports/cli/report_lib.py", False),
]

_TAR_PATH = None
_MANIFEST_PATH = None


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_package(tar_path, manifest_path, unpack_dir, expected):
    """Unpacks tar_path into unpack_dir and returns a list of problems (empty = OK)."""
    problems = []
    with open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    with tarfile.open(tar_path) as tar:
        members = {m.name: m for m in tar.getmembers() if m.isfile()}
        tar.extractall(unpack_dir, filter="fully_trusted")  # our own build output

    listed = set()
    for entry in manifest:
        path = entry["path"]
        if path in listed:
            problems.append("manifest lists %s twice" % path)
        listed.add(path)
        if path not in members:
            problems.append("manifest entry %s missing from tar" % path)
            continue
        unpacked = os.path.join(unpack_dir, path)
        if not os.path.isfile(unpacked):
            problems.append("manifest entry %s missing after unpack" % path)
            continue
        got = _sha256(unpacked)
        if got != entry["sha256"]:
            problems.append("sha256 mismatch for %s: manifest %s, unpacked %s" % (path, entry["sha256"], got))

    for name in sorted(set(members) - listed):
        problems.append("tar member %s is not listed in the manifest" % name)

    for path, must_exec in expected:
        unpacked = os.path.join(unpack_dir, path)
        if not os.path.isfile(unpacked):
            problems.append("expected file %s is missing" % path)
        elif must_exec and not os.access(unpacked, os.X_OK):
            problems.append("expected executable %s is not executable" % path)
    return problems


def _write_package(root, files):
    """files: {path: (bytes, mode)}. Returns (tar_path, manifest_path)."""
    tar_path = os.path.join(root, "pkg.tar")
    manifest = []
    with tarfile.open(tar_path, "w") as tar:
        for path in sorted(files):
            data, mode = files[path]
            info = tarfile.TarInfo(path)
            info.size = len(data)
            info.mode = mode
            tar.addfile(info, io.BytesIO(data))
            manifest.append({"path": path, "sha256": hashlib.sha256(data).hexdigest(), "label": "//x:y"})
    manifest_path = os.path.join(root, "pkg.MANIFEST.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f)
    return tar_path, manifest_path


class CheckerNegativeTest(unittest.TestCase):
    """The checker must fail on a tampered package (it is not vacuously passing)."""

    FILES = {"a/bin/tool": (b"#!/bin/sh\n", 0o755), "a/data.txt": (b"hello\n", 0o644)}
    EXPECTED = [("a/bin/tool", True), ("a/data.txt", False)]

    def _run(self, tar_path, manifest_path):
        with tempfile.TemporaryDirectory() as out:
            return check_package(tar_path, manifest_path, out, self.EXPECTED)

    def test_clean_fixture_passes(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self._run(*_write_package(d, self.FILES)), [])

    def test_tampered_file_fails(self):
        with tempfile.TemporaryDirectory() as d:
            tar_path, manifest_path = _write_package(d, self.FILES)
            tampered = dict(self.FILES)
            tampered["a/data.txt"] = (b"HELLO\n", 0o644)
            os.makedirs(os.path.join(d, "t"))
            bad_tar, _ = _write_package(os.path.join(d, "t"), tampered)
            problems = self._run(bad_tar, manifest_path)
            self.assertTrue(any("sha256 mismatch for a/data.txt" in p for p in problems), problems)

    def test_missing_executable_fails(self):
        files = dict(self.FILES)
        files["a/bin/tool"] = (b"#!/bin/sh\n", 0o644)
        with tempfile.TemporaryDirectory() as d:
            problems = self._run(*_write_package(d, files))
            self.assertTrue(any("not executable" in p for p in problems), problems)

    def test_missing_file_fails(self):
        files = {"a/data.txt": self.FILES["a/data.txt"]}
        with tempfile.TemporaryDirectory() as d:
            problems = self._run(*_write_package(d, files))
            self.assertTrue(any("a/bin/tool is missing" in p for p in problems), problems)

    def test_unlisted_member_fails(self):
        with tempfile.TemporaryDirectory() as d:
            tar_path, manifest_path = _write_package(d, self.FILES)
            with open(manifest_path, encoding="utf-8") as f:
                manifest = json.load(f)
            with open(manifest_path, "w", encoding="utf-8") as f:
                json.dump([e for e in manifest if e["path"] != "a/data.txt"], f)
            problems = self._run(tar_path, manifest_path)
            self.assertTrue(any("not listed in the manifest" in p for p in problems), problems)


class RealPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR"))
        cls.problems = check_package(_TAR_PATH, _MANIFEST_PATH, cls.dir.name, EXPECTED)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def test_manifest_and_executables(self):
        self.assertEqual(self.problems, [], "\n".join(self.problems))

    def test_manifest_is_not_trivial(self):
        with open(_MANIFEST_PATH, encoding="utf-8") as f:
            self.assertGreater(len(json.load(f)), len(EXPECTED))

    def test_gateway_jar_is_a_runnable_jar(self):
        jar = os.path.join(self.dir.name, "gateway/release/gateway_deploy.jar")
        with zipfile.ZipFile(jar) as z:
            manifest = z.read("META-INF/MANIFEST.MF").decode()
            self.assertIn("Main-Class: analytics.gateway.MainKt", manifest.replace("\r", ""))
            self.assertIn("analytics/gateway/MainKt.class", z.namelist())

    def test_unpacked_agent_runs(self):
        agent = os.path.join(self.dir.name, "agent/ingest/agent/main/agent_/agent")
        r = subprocess.run([agent], capture_output=True, text=True, timeout=30, env={})
        self.assertEqual(r.returncode, 2, r.stderr)
        self.assertIn("usage: agent", r.stderr)

    def test_unpacked_engine_starts(self):
        engine = os.path.join(self.dir.name, "engine/engine/service/engine")
        out = os.path.join(self.dir.name, "engine.out")
        with open(out, "w") as fo:
            p = subprocess.Popen([engine, "--port=0", "--host=127.0.0.1"], stdout=fo, stderr=subprocess.STDOUT, env={})
        try:
            deadline = time.time() + 30
            line = ""
            while time.time() < deadline and not line:
                with open(out) as f:
                    line = f.readline().strip()
                    self.assertTrue(line or p.poll() is None, "engine exited early")
                time.sleep(0.1)
            self.assertRegex(line, r"^LISTENING \d+$")
        finally:
            p.kill()
            p.wait()


if __name__ == "__main__":
    # Bazel passes one space-separated argument ($(rootpaths :analytics)) or two separate paths.
    paths = " ".join(sys.argv[1:]).split()
    tars = [p for p in paths if p.endswith(".tar")]
    manifests = [p for p in paths if p.endswith(".MANIFEST.json")]
    if len(tars) != 1 or len(manifests) != 1:
        sys.exit("usage: package_test.py <analytics.tar> <analytics.MANIFEST.json>; got %r" % paths)
    _TAR_PATH, _MANIFEST_PATH = tars[0], manifests[0]
    unittest.main(argv=[sys.argv[0]])
