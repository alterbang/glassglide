import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.launcher = self.root / "glassglide-launch"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-static",
            str(ROOT / "src/launcher.c"), "-o", str(self.launcher)
        ], check=True)
        (self.root / "bin").mkdir()
        self.helpers = self.root / "libexec/wpe-webkit-2.0"
        self.helpers.mkdir(parents=True)
        bundle = self.root / "lib/wpe-webkit-2.0/injected-bundle"
        bundle.mkdir(parents=True)
        (self.root / "lib/libWPEWebKit-2.0.so.1").touch()
        (bundle / "libWPEInjectedBundle.so").touch()
        app = self.root / "bin/glassglide"
        app.write_text("#!/bin/sh\nenv\n", encoding="ascii")
        app.chmod(0o755)
        for name in ("WPEWebProcess", "WPENetworkProcess"):
            helper = self.helpers / name
            helper.write_text("#!/bin/sh\nexit 0\n", encoding="ascii")
            helper.chmod(0o755)

    def test_private_paths_and_debug_environment(self):
        env = dict(os.environ, LD_PRELOAD="/invalid/test.so",
                   WEBKIT_INSPECTOR_SERVER="0.0.0.0:1234",
                   WPE_PLATFORMS_PATH="/invalid/modules")
        env.pop("XDG_RUNTIME_DIR", None)
        env.pop("WAYLAND_DISPLAY", None)
        result = subprocess.run([str(self.launcher)], env=env, text=True,
                                capture_output=True, check=True)
        variables = dict(line.split("=", 1) for line in result.stdout.splitlines())
        self.assertEqual(variables["LD_LIBRARY_PATH"], str(self.root / "lib"))
        self.assertEqual(variables["WEBKIT_EXEC_PATH"], str(self.helpers))
        self.assertEqual(variables["WEBKIT_INJECTED_BUNDLE_PATH"],
                         str(self.root / "lib/wpe-webkit-2.0/injected-bundle"))
        self.assertEqual(variables["GIO_MODULE_DIR"], str(self.root / "lib/gio/modules"))
        self.assertEqual(variables["XDG_RUNTIME_DIR"], "/tmp/xdg")
        self.assertEqual(variables["WAYLAND_DISPLAY"], "wayland-0")
        self.assertNotIn("LD_PRELOAD", variables)
        self.assertNotIn("WEBKIT_INSPECTOR_SERVER", variables)
        self.assertNotIn("WPE_PLATFORMS_PATH", variables)

    def test_incomplete_runtime_does_not_start_app(self):
        (self.helpers / "WPENetworkProcess").unlink()
        result = subprocess.run([str(self.launcher)], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incomplete private WPE runtime", result.stderr)

    def test_compositor_setting_overrides_inherited_environment(self):
        env = dict(os.environ, WEBKIT_USE_SKIA_FOR_COMPOSITION="1")
        result = subprocess.run([str(self.launcher)], env=env, text=True,
                                capture_output=True, check=True)
        variables = dict(line.split("=", 1) for line in result.stdout.splitlines())
        self.assertEqual(variables["WEBKIT_USE_SKIA_FOR_COMPOSITION"], "0")

    def test_missing_private_bundle_does_not_start_app(self):
        (self.root / "lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so").unlink()
        result = subprocess.run([str(self.launcher)], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incomplete private WPE runtime", result.stderr)


if __name__ == "__main__":
    unittest.main()
