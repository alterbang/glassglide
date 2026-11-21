from pathlib import Path
import re
import subprocess
import unittest
from unittest.mock import patch

from tools.audit_elf import inspect_elf


ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / ".deps/buildroot-output/target/usr/lib/libexpat.so.1.12.4"


@unittest.skipUnless(SAMPLE.is_file(), "build target dependencies first")
class ElfAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.output = subprocess.check_output(
            ["readelf", "--wide", "-h", "-l", "-d", "-V", "-A", str(SAMPLE)], text=True
        )

    def inspect_output(self, output):
        completed = subprocess.CompletedProcess([], 0, stdout=output, stderr="")
        with patch("tools.audit_elf.subprocess.run", return_value=completed):
            return inspect_elf(SAMPLE)

    def test_valid_target_elf(self):
        self.assertIn("libc.so.6", inspect_elf(SAMPLE).needed)

    def test_nonadjacent_relocations_are_rejected(self):
        output, count = re.subn(r"(\(JMPREL\)\s+)0x[0-9a-fA-F]+",
                                r"\g<1>0x1", self.output, count=1)
        self.assertEqual(count, 1)
        with self.assertRaisesRegex(ValueError, "not adjacent"):
            self.inspect_output(output)

    def test_newer_glibc_requirement_is_rejected(self):
        output, count = re.subn(r"Name:\s+GLIBC_\d+(?:\.\d+)+",
                                "Name: GLIBC_2.28", self.output, count=1)
        self.assertEqual(count, 1)
        with self.assertRaisesRegex(ValueError, "newer than"):
            self.inspect_output(output)

    def test_hard_float_abi_is_rejected(self):
        output, count = re.subn(r"(Flags:\s+)0x[0-9a-fA-F]+",
                                r"\g<1>0x5000400", self.output, count=1)
        self.assertEqual(count, 1)
        with self.assertRaisesRegex(ValueError, "hard-float"):
            self.inspect_output(output)

    def test_newer_arm_architecture_is_rejected(self):
        output, count = re.subn(r"(Tag_CPU_arch:\s+)v7", r"\g<1>v8", self.output, count=1)
        self.assertEqual(count, 1)
        with self.assertRaisesRegex(ValueError, "ARMv7-A"):
            self.inspect_output(output)

    def test_incompatible_float_attributes_are_rejected(self):
        output = self.output + "\n  Tag_ABI_VFP_args: VFP registers\n"
        with self.assertRaisesRegex(ValueError, "soft-float"):
            self.inspect_output(output)


if __name__ == "__main__":
    unittest.main()
