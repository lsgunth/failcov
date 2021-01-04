#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import enum
import unittest
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).absolute().parent

class TestCode(enum.IntEnum):
    SUCCESS = 0
    EXPECTED_ERROR = 1
    FAILCOV_ERROR = 32
    FAILCOV_BUG_FOUND = 33
    SEGFAULT = -11

    MEM_LEAK = 1000
    FD_LEAK = 1001
    FILE_LEAK = 1002

class FailCovTestCase(unittest.TestCase):
    _expected_codes = [
        (TestCode.SEGFAULT,            "x allocation failed"),
        (TestCode.MEM_LEAK,            "y allocation failed"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/zero"),
        (TestCode.FD_LEAK,             "Failed to read /dev/zero"),
        (TestCode.SUCCESS,             "close injected failure"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/urandom"),
        (TestCode.FD_LEAK,             "Failed to read /dev/urandom"),
        (TestCode.EXPECTED_ERROR,      "Error closing /dev/urandom"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/null"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/null"),
        (TestCode.FILE_LEAK,           "Unable to write to /dev/null"),
        (TestCode.SUCCESS,             "fwrite/malloc injected failure"),
        (TestCode.FILE_LEAK,           "Error while flushing to /dev/null"),
        (TestCode.EXPECTED_ERROR,      "Error while to closing /dev/null"),
        (TestCode.EXPECTED_ERROR,      "Unable to open memory FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to open memory FILE (mem)"),
        (TestCode.EXPECTED_ERROR,      "Unable to open memory FILE (mem2)"),
        (TestCode.EXPECTED_ERROR,      "Failure closing memory FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to open temporary FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to open temporary FILE (mem)"),
        (TestCode.EXPECTED_ERROR,      "Failure closing temporary FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to creat temporary file"),
        (TestCode.EXPECTED_ERROR,      "Unable to fdopen temporary file"),
        (TestCode.EXPECTED_ERROR,      "Unable to fdopen temporary file (mem)"),
        (TestCode.EXPECTED_ERROR,      "Failure closing temporary FILE"),
        (TestCode.SUCCESS,             "printf/malloc injected failure"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/urandom"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/urandom (mem)"),
        (TestCode.FILE_LEAK,           "Unable to open /dev/random"),
        (TestCode.FILE_LEAK,           "Unable to open /dev/random (mem)"),
        (TestCode.EXPECTED_ERROR,      "Error while closing all files"),
        (TestCode.SUCCESS,             "no failures"),
    ]

    def run_test(self, db, env={}):
        env["LD_PRELOAD"] = str(ROOT / "failcov.so")
        env["FAILCOV_DATABASE"] = str(db)
        return subprocess.run(["./test"], cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)
        print(p.stdout)
        return p.returncode

    def expected_codes(self, env):
        for ec, title in self._expected_codes:
            if ec == TestCode.MEM_LEAK:
                if "FAILCOV_IGNORE_ALL_MEM_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILCOV_BUG_FOUND, title
            elif ec == TestCode.FD_LEAK:
                if "FAILCOV_IGNORE_ALL_FD_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILCOV_BUG_FOUND, title
            elif ec == TestCode.FILE_LEAK:
                if "FAILCOV_IGNORE_ALL_FILE_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILCOV_BUG_FOUND, title
            else:
                yield ec, title

    def run_tests(self, env={}):
        with tempfile.NamedTemporaryFile() as db:
            for i, (ec, t) in enumerate(self.expected_codes(env)):
                with self.subTest(t):
                    p = self.run_test(db.name, env=env)
                    if ec != p.returncode:
                        print(f" ----- {i}: {t} -----")
                        print(p.stdout)
                    self.assertEqual(ec, p.returncode)

    def test_normal_run(self):
        self.run_tests()

    def test_ignore_mem_leaks(self):
        self.run_tests(env={"FAILCOV_ALL_IGNORE_MEM_LEAKS": "y"})

    def test_ignore_fd_leaks(self):
        self.run_tests(env={"FAILCOV_ALL_IGNORE_FD_LEAKS": "y"})

    def test_ignore_file_leaks(self):
        self.run_tests(env={"FAILCOV_ALL_IGNORE_FILE_LEAKS": "y"})

if __name__ == '__main__':
        unittest.main(buffer=True, catchbreak=True)
