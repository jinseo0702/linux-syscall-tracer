"""Run with python3 portfolio_audit/tests/test_trace_regressions.py on x86 Linux."""

import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest


TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parent.parent


class TraceRegressions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.build = Path(cls.temp.name)
        cls.tracer = cls.build / "ft_strace"
        flags = ["gcc", "-Wall", "-Wextra", "-Werror"]
        subprocess.run([*flags, "-D_GNU_SOURCE", ROOT / "ft_strace.c", "-o", cls.tracer], check=True)
        cases = ("t02_write_efault", "t03_read_pipe", "t05_lseek64", "exec_target",
                 "t09_execve", "t14_exit_status", "read_buffer_edges")
        for name in cases:
            subprocess.run([*flags, TESTS / (name + ".c"), "-o", cls.build / name], check=True)

    def trace(self, name, *args, status=0):
        result = subprocess.run([self.tracer, self.build / name, *args], capture_output=True,
                                env={**os.environ, "LC_ALL": "C"}, timeout=5)
        output = result.stderr.decode("utf-8", "replace")
        self.assertEqual(result.returncode, status, output)
        return output

    def test_fault_address(self):
        self.assertIn("write(1, 0x1, 16) = -1 Bad address", self.trace("t02_write_efault"))

    def test_read_on_exit(self):
        self.assertRegex(self.trace("t03_read_pipe"), r'read\(\d+, "READ", 4\) = 0x4')

    def test_offset_width(self):
        self.assertRegex(self.trace("t05_lseek64"), r'lseek\(\d+, 4294967298, 0\) = 0x100000002')

    def test_exec_exit_status(self):
        output = self.trace("t09_execve", self.build / "exec_target", status=23)
        self.assertIn('"alpha", "beta"', output)
        self.assertIn("+++ exited with 23 +++", output)

    def test_exit_status(self):
        self.assertIn("+++ exited with 7 +++", self.trace("t14_exit_status", status=7))

    def test_i386_errno(self):
        subprocess.run(["as", "--32", TESTS / "t15_i386_smoke.S", "-o", self.build / "compat.o"], check=True)
        subprocess.run(["ld", "-m", "elf_i386", self.build / "compat.o",
                        "-o", self.build / "compat"], check=True)
        output = self.trace("compat")
        self.assertIn("runs in 32 bit mode", output)
        self.assertIn("write(1, 0x1, 16) = -1 Bad address", output)

    def test_buffer_limits(self):
        output = self.trace("read_buffer_edges")
        self.assertRegex(output, r'read\(\d+, "Z", 4\) = 0x1')
        self.assertRegex(output, r'read\(\d+, "", 4\) = 0x0')
        self.assertRegex(output, r'read\(-1, 0x[0-9a-f]+, 4\) = -1 Bad file descriptor')
        self.assertRegex(output, r'write\(\d+, "Q"\.\.\., 2\) = 0x2')

    def test_signal_status(self):
        self.assertIn("+++ killed by SIGTERM +++",
                      self.trace("/bin/sh", "-c", "kill -TERM $$", status=-signal.SIGTERM))

    def test_long_argv(self):
        text = "x" * 3000
        output = self.trace("/bin/true", text, text)
        self.assertIn(f'"{text}", "{text}"', output)


if __name__ == "__main__":
    unittest.main()
