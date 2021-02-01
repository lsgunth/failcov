#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

import enum
import unittest
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).absolute().parent

class TestCode(enum.IntEnum):
    SUCCESS = 0
    EXPECTED_ERROR = 1
    FAILINJ_ERROR = 32
    FAILINJ_BUG_FOUND = 33
    FAILINJ_DONE = 34
    SEGFAULT = -11

    MEM_LEAK = 1000
    FD_LEAK = 1001
    FILE_LEAK = 1002
    CLOSE_UNTRACKED = 1003
    IGNORE_MEM_LEAK = 1004
    SKIPPED = 1005

class FailCovTestCase(unittest.TestCase):
    _expected_codes = [
        (TestCode.SEGFAULT,            "x allocation failed"),
        (TestCode.MEM_LEAK,            "y allocation failed"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/zero"),
        (TestCode.FD_LEAK,             "Failed to read /dev/zero"),
        (TestCode.CLOSE_UNTRACKED,     "Failed to write /dev/zero"),
        (TestCode.SUCCESS,             "close injected failure"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/urandom"),
        (TestCode.FD_LEAK,             "Failed to read /dev/urandom"),
        (TestCode.EXPECTED_ERROR,      "Error closing /dev/urandom"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/null"),
        (TestCode.FILE_LEAK,           "Unable to write to /dev/null"),
        (TestCode.FILE_LEAK,           "Unable to read from /dev/null"),
        (TestCode.FILE_LEAK,           "Unable to scan from /dev/null"),
        (TestCode.FILE_LEAK,           "getline failure"),
        (TestCode.FILE_LEAK,           "getdelim failure"),
        (TestCode.FILE_LEAK,           "Error while flushing to /dev/null"),
        (TestCode.EXPECTED_ERROR,      "Error while to closing /dev/null"),
        (TestCode.EXPECTED_ERROR,      "sscanf failed"),
        (TestCode.EXPECTED_ERROR,      "Unable to open memory FILE"),
        (TestCode.EXPECTED_ERROR,      "Failure closing memory FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to open temporary FILE"),
        (TestCode.EXPECTED_ERROR,      "Failure closing temporary FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to creat temporary file"),
        (TestCode.EXPECTED_ERROR,      "Unable to fdopen temporary file"),
        (TestCode.EXPECTED_ERROR,      "Failure closing temporary FILE"),
        (TestCode.EXPECTED_ERROR,      "Unable to calloc memory"),
        (TestCode.EXPECTED_ERROR,      "Unable to realloc memory"),
        (TestCode.EXPECTED_ERROR,      "Unable to reallocarray memory"),
        (TestCode.EXPECTED_ERROR,      "Unable to allocate leaked memory"),
        (TestCode.IGNORE_MEM_LEAK,     "Unable to allocate ignored leak memory"),
        (TestCode.SKIPPED,             "Unable to allocate skipped malloc"),
        (TestCode.EXPECTED_ERROR,      "test_hash_table"),
        (TestCode.EXPECTED_ERROR,      "Unable to open /dev/urandom"),
        (TestCode.FILE_LEAK,           "Unable to open /dev/random"),
        (TestCode.EXPECTED_ERROR,      "Error while closing all files"),
        (TestCode.FAILINJ_DONE,        "no failures"),
    ]

    _expected_test3_codes = [
        (TestCode.EXPECTED_ERROR,      "sscanf failed"),
        (TestCode.EXPECTED_ERROR,      "Could not open /dev/zero"),
        (TestCode.EXPECTED_ERROR,      "fscanf failed"),
        (TestCode.SUCCESS,             "fclose failure"),
        (TestCode.EXPECTED_ERROR,      "Could not open /dev/null"),
        (TestCode.EXPECTED_ERROR,      "getline failure"),
        (TestCode.SUCCESS,             "fclose failure"),
        (TestCode.FAILINJ_DONE,        "no fails"),
    ]

    def _run_test(self, db, env=None, payload=None, args=[]):
        if payload is None:
            payload = "./test"
        if env is None:
            env = {}
        env.setdefault("LD_PRELOAD", str(ROOT / "libfailinj.so"))
        if db is not None:
            env["FAILINJ_DATABASE"] = str(db)
        return subprocess.run([payload] + args, cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)

    def run_test(self, *args, **kws):
        p = self._run_test(*args, **kws)
        print(p.stdout)
        return p

    def expected_codes(self, env, expected_codes=[]):
        for ec, title in expected_codes:
            if ec == TestCode.MEM_LEAK:
                if "FAILINJ_IGNORE_ALL_MEM_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILINJ_BUG_FOUND, title
            elif ec == TestCode.FD_LEAK:
                if "FAILINJ_IGNORE_ALL_FD_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILINJ_BUG_FOUND, title
            elif ec == TestCode.FILE_LEAK:
                if "FAILINJ_IGNORE_ALL_FILE_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILINJ_BUG_FOUND, title
            elif ec == TestCode.CLOSE_UNTRACKED:
                if "FAILINJ_IGNORE_ALL_UNTRACKED_CLOSES" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILINJ_BUG_FOUND, title
            elif ec == TestCode.IGNORE_MEM_LEAK:
                if "FAILINJ_IGNORE_MEM_LEAKS" in env:
                    yield TestCode.EXPECTED_ERROR, title
                else:
                    yield TestCode.FAILINJ_BUG_FOUND, title
            elif ec == TestCode.SKIPPED:
                if "FAILINJ_SKIP_INJECTION" in env:
                    continue
                else:
                    yield TestCode.EXPECTED_ERROR, title
            else:
                yield ec, title

    def run_tests(self, payload=None, env={}, expected_codes=None):
        if expected_codes is None:
            expected_codes = self._expected_codes

        with tempfile.NamedTemporaryFile() as db:
            exp = self.expected_codes(env, expected_codes)
            for i, (ec, t) in enumerate(exp):
                with self.subTest(t):
                    p = self._run_test(db.name, env=env, payload=payload)
                    if ec != p.returncode:
                        print(f" ----- {i}: {t} -----")
                        print(p.stdout)
                    self.assertEqual(ec, p.returncode)

    def test_normal_run(self):
        self.run_tests()

    def test_ignore_mem_leaks(self):
        self.run_tests(env={"FAILINJ_ALL_IGNORE_MEM_LEAKS": "y"})

    def test_ignore_fd_leaks(self):
        self.run_tests(env={"FAILINJ_ALL_IGNORE_FD_LEAKS": "y"})

    def test_ignore_file_leaks(self):
        self.run_tests(env={"FAILINJ_ALL_IGNORE_FILE_LEAKS": "y"})

    def test_ignore_untracked_closes(self):
        self.run_tests(env={"FAILINJ_IGNORE_ALL_UNTRACKED_CLOSES": "y"})

    def test_ignore_specific(self):
        self.run_tests(env={"FAILINJ_IGNORE_MEM_LEAKS": "test_ignore_leak"})

    def test_skip_specific(self):
        self.run_tests(env={"FAILINJ_SKIP_INJECTION": "test_skip_failure"})

    def test_invalid_db(self):
        p = self.run_test("/not/a/valid/path/123/database")
        self.assertEqual(TestCode.FAILINJ_ERROR, p.returncode)

    def test_custom_exit_err(self):
        err = 52
        p = self.run_test("/not/a/valid/path/123/database",
                          env={"FAILINJ_EXIT_ERROR": str(err)})
        self.assertEqual(52, p.returncode)

    def test_fulldb(self):
        p = self.run_test("/dev/full")
        self.assertEqual(TestCode.FAILINJ_ERROR, p.returncode)

    def test_nodb(self):
        self.run_test(None)
        self.run_test(None)
        os.unlink("failinj.db")

    def test_test3(self):
        self.run_tests(payload="./test3",
                       expected_codes=self._expected_test3_codes)

    def check_no_segfault(self, db, iterations=25, payload=None, env=None,
                          allow_failinj_err=False):
        exp = (TestCode.SUCCESS,
               TestCode.EXPECTED_ERROR,
               TestCode.FAILINJ_BUG_FOUND,
               TestCode.FAILINJ_DONE)

        if allow_failinj_err:
            exp += (TestCode.FAILINJ_ERROR, )

        for i in range(iterations):
            with self.subTest(i=i):
                p = self._run_test(db.name, payload=payload,
                                   args=["dontsegfault"], env=env)
                print(f" ----- {i} -----")
                print(p.stdout)

                self.assertIn(p.returncode, exp)

    def test_stripped(self):
        #Stripped executables won't work correctly, but ensure they don't crash

        with tempfile.TemporaryDirectory() as tmpdirname:
            s = pathlib.Path(tmpdirname) / "test_stripped"
            subprocess.check_call(["strip", str(ROOT / "test"), "-o", str(s)])
            with tempfile.NamedTemporaryFile() as db:
                self.check_no_segfault(db, payload=str(s))

    def test_zzdogfood(self):
        """Test the library with itself to check the final corner cases.
           There isn't much checking save for ensuring it doesn't segfault"""

        with tempfile.TemporaryDirectory() as tmpdirname:
            lib = ROOT / "libfailinj.so"
            lib2 = ROOT / "libfailinj2.so"

            with tempfile.NamedTemporaryFile() as db, \
                 tempfile.NamedTemporaryFile() as db2:
                env = {"LD_PRELOAD": f"{lib} {lib2}",
                       "FAILINJ2_DATABASE": db2.name,
                       "FAILINJ2_IGNORE_FILE_LEAKS": "should_fail",
                       "FAILINJ2_IGNORE_ALL_UNTRACKED_FREES": "y",
                       "FAILINJ2_IGNORE_ALL_UNTRACKED_CLOSES": "y",
                       "FAILINJ2_IGNORE_ALL_MEM_LEAKS": "y",
                       "FAILINJ2_SKIP_INJECTION": "_ULx86_64_get_proc_name",
                       "FAILINJ_IGNORE_MEM_LEAKS": "none",
                       "FAILINJ_IGNORE_UNTRACKED_CLOSES": "none",
                      }

                for j in range(3):
                    with self.subTest(j=j):
                        p = self._run_test(db.name, args=["dontsegfault"],
                                           env=env, payload="./test2")
                        print(f" ----- initial {j} -----")
                        print(p.stdout)
                        self.assertEqual(p.returncode, TestCode.FAILINJ_ERROR)

                self.check_no_segfault(db, env=env, payload="./test2",
                                       allow_failinj_err=True,
                                       iterations=15)

if __name__ == '__main__':
        unittest.main(buffer=True, catchbreak=True)
