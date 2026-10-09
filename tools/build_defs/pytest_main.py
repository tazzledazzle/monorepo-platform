"""Shared entry point that runs pytest for every analytics_py_test.

The macro passes the test files as arguments (as runfiles-relative paths), so one shim
serves every test target. Bazel's XML_OUTPUT_FILE is honoured so results show up in
`bazel test` reports.
"""

import os
import sys

import pytest

if __name__ == "__main__":
    args = ["-p", "no:cacheprovider", "-ra"] + sys.argv[1:]
    xml = os.environ.get("XML_OUTPUT_FILE")
    if xml:
        args.append("--junitxml=" + xml)
    sys.exit(pytest.main(args))
