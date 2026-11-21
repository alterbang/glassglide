import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".deps/buildroot-output"
SYSROOT = OUTPUT / "host/arm-buildroot-linux-gnueabi/sysroot"
SDK = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
TARGET = os.environ.get("GLASSGLIDE_WPE_TEST_TARGET")
TEST_LIBRARY = os.environ.get("GLASSGLIDE_WPE_TEST_LIBRARY")
APP_LIB = "/media/developer/apps/usr/palm/applications/org.glassglide.browser/lib"


@unittest.skipUnless(TARGET, "set GLASSGLIDE_WPE_TEST_TARGET for the isolated TV test")
class WpeMultiSeatTest(unittest.TestCase):
    def test_keyboards_focus_and_hotplug(self):
        if not re.fullmatch(
            r"(?:[a-zA-Z0-9_][a-zA-Z0-9._-]*@)?[a-zA-Z0-9][a-zA-Z0-9._-]*", TARGET
        ):
            self.fail("invalid SSH target")
        ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", TARGET]
        meminfo = subprocess.check_output(
            ssh + ["cat", "/proc/meminfo"], text=True, timeout=15
        )
        values = dict(re.findall(r"^(MemAvailable|SwapFree):\s+(\d+)", meminfo, re.M))
        if int(values.get("MemAvailable", 0)) < 225280 or int(values.get("SwapFree", 0)) < 196608:
            self.fail("TV memory or swap is insufficient or unavailable for the isolated WPE test")

        with tempfile.TemporaryDirectory(prefix="glassglide-multiseat-") as temp:
            executable = Path(temp) / "harness"
            environment = dict(
                os.environ,
                PKG_CONFIG_SYSROOT_DIR=str(SYSROOT),
                PKG_CONFIG_LIBDIR="{}:{}".format(
                    SYSROOT / "usr/lib/pkgconfig", SYSROOT / "usr/share/pkgconfig"
                ),
            )
            flags = subprocess.check_output(
                [str(OUTPUT / "host/bin/pkg-config"), "--cflags", "--libs",
                 "wpe-platform-wayland-2.0", "wayland-server", "xkbcommon"],
                env=environment, text=True,
            )
            subprocess.run(
                [str(SDK / "bin/arm-webos-linux-gnueabi-gcc"),
                 "--sysroot=" + str(SYSROOT), "-std=c11", "-O2",
                 "-Wall", "-Wextra", "-Werror",
                 str(ROOT / "tests/wpe_multiseat_harness.c"), *shlex.split(flags),
                 "-Wl,-rpath-link," + str(SYSROOT / "usr/lib"), "-o", str(executable)],
                check=True,
            )
            directory = subprocess.check_output(
                ssh + ["mktemp -d /tmp/glassglide-wpe-multiseat.XXXXXX"],
                text=True, timeout=15,
            ).strip()
            if not re.fullmatch(r"/tmp/glassglide-wpe-multiseat\.[A-Za-z0-9]{6}", directory):
                self.fail("unexpected remote temporary directory")
            try:
                subprocess.run(ssh + ["chmod 711 " + directory], check=True, timeout=15)
                scp = ["scp", "-O", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
                files = [
                    (executable, "harness"),
                    (OUTPUT / "target/usr/lib/libwayland-server.so.0", "libwayland-server.so.0"),
                ]
                if TEST_LIBRARY:
                    library = Path(temp) / "libWPEWebKit-2.0.so.1"
                    shutil.copyfile(TEST_LIBRARY, library)
                    subprocess.run(
                        [str(SDK / "bin/arm-webos-linux-gnueabi-strip"),
                         "--strip-unneeded", str(library)], check=True,
                    )
                    files.append((library, library.name))
                for source, name in files:
                    subprocess.run(
                        scp + [str(source), "{}:{}/{}".format(TARGET, directory, name)],
                        check=True, timeout=60,
                    )
                result = subprocess.run(
                    ssh + [
                        "chmod 755 {0}/harness {0}/libwayland-server.so.0 && "
                        "timeout -t 25 setuidgid nobody /usr/bin/env "
                        "LD_LIBRARY_PATH={0}:{1} XKB_CONFIG_ROOT=/usr/share/X11/xkb "
                        "{0}/harness".format(directory, APP_LIB)
                    ],
                    capture_output=True, text=True, timeout=35,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("Multiple keyboards and pointers, shared focus, hotplug and primary-seat loss: OK",
                              result.stdout)
            finally:
                subprocess.run(
                    ssh + [
                        "rm -f {0}/harness {0}/libwayland-server.so.0 "
                        "{0}/libWPEWebKit-2.0.so.1 && rmdir {0}".format(directory)
                    ],
                    check=True, timeout=15,
                )


if __name__ == "__main__":
    unittest.main()
