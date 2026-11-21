import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class HostPlatformTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="glassglide-host-test-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "tools").mkdir()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name in ("host-platform.sh", "prepare-deps.sh"):
            shutil.copy2(ROOT / "tools" / name, self.root / "tools" / name)
        self.fake_tool("uname", """\
case "$1" in
    -s) echo Linux ;;
    -m) echo "$TEST_MACHINE" ;;
    *) exit 1 ;;
esac
""")
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"])

    def fake_tool(self, name, body):
        tool = self.bin / name
        tool.write_text("#!/bin/sh\n" + body, encoding="utf-8")
        tool.chmod(0o755)

    def prepare(self, machine):
        return subprocess.run(
            [str(self.root / "tools/prepare-deps.sh")],
            env=dict(self.env, TEST_MACHINE=machine),
            capture_output=True, text=True,
        )

    def test_preparation_selects_native_sdk_and_clang_packages(self):
        self.fake_tool("curl", """\
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output) destination=$2; shift 2 ;;
        *) url=$1; shift ;;
    esac
done
printf '%s\\n' "$url" >> "$TEST_URL_LOG"
: > "$destination"
""")
        self.fake_tool("sha256sum", "cat >/dev/null\n")
        self.fake_tool("dpkg-deb", 'printf "%s\\n" "$2" > "$TEST_PACKAGE_LOG"\nexit 42\n')
        self.env["TEST_URL_LOG"] = str(self.root / "urls")
        self.env["TEST_PACKAGE_LOG"] = str(self.root / "package")

        for machine, deb_arch in (("aarch64", "arm64"), ("x86_64", "amd64")):
            with self.subTest(machine=machine):
                result = self.prepare(machine)
                self.assertEqual(result.returncode, 42, result.stderr)
                urls = (self.root / "urls").read_text().splitlines()
                self.assertEqual(len(urls), 7)
                self.assertTrue(urls[0].endswith("sdk-buildroot-" + machine + ".tar.gz"))
                self.assertEqual(sum("snapshot.debian.org/file/" in url for url in urls), 4)
                self.assertTrue(
                    (self.root / "package").read_text().strip().endswith(
                        "clang-19_19.1.7-3+b1_" + deb_arch + ".deb"
                    )
                )
                shutil.rmtree(self.root / ".deps")
                (self.root / "urls").unlink()
                (self.root / "package").unlink()

    def test_rejects_other_host_architecture_before_downloading(self):
        deps = self.root / ".deps"
        deps.mkdir()
        marker = deps / ".host-arch"
        marker.write_text("aarch64\n", encoding="ascii")
        result = self.prepare("x86_64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("another host architecture", result.stderr)
        self.assertEqual(marker.read_text(), "aarch64\n")
        self.assertFalse((deps / "downloads").exists())

    def test_rejects_legacy_incompatible_sdk_without_marker(self):
        sdk = self.root / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/bin"
        sdk.mkdir(parents=True)
        (sdk / "cmake").touch()
        self.fake_tool("readelf", 'printf "  Machine: AArch64\\n"\n')
        result = self.prepare("x86_64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Incompatible host binary", result.stderr)
        self.assertFalse((self.root / ".deps/downloads").exists())

    def test_rejects_unsupported_host(self):
        result = self.prepare("riscv64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unsupported host", result.stderr)
        self.assertFalse((self.root / ".deps").exists())


if __name__ == "__main__":
    unittest.main()
