from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.audit_elf import inspect_elf
from tools.package import (
    REQUIRED_WPE_CONFIGURATION, bundle_libraries, normalize_elf, verify_wpe_configuration,
)


ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / ".deps/buildroot-output/target"
FONTCONFIG = TARGET / "usr/lib/libfontconfig.so.1.16.0"
SDK = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
GCC = SDK / "bin/arm-webos-linux-gnueabi-gcc"
GXX = SDK / "bin/arm-webos-linux-gnueabi-g++"
SDK_RUNTIME = SDK / "arm-webos-linux-gnueabi/lib"
WPE_LIBRARY = TARGET / "usr/lib/libWPEWebKit-2.0.so.1"


class BuildConfigurationTests(unittest.TestCase):
    def test_insecure_inspector_build_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            header = Path(temp) / "cmakeconfig.h"
            definitions = dict(REQUIRED_WPE_CONFIGURATION)
            header.write_text("".join(
                "#define {} {}\n".format(name, value) for name, value in definitions.items()
            ))
            verify_wpe_configuration(header)
            definitions["ENABLE_REMOTE_INSPECTOR"] = "1"
            header.write_text("".join(
                "#define {} {}\n".format(name, value) for name, value in definitions.items()
            ))
            with self.assertRaisesRegex(ValueError, "ENABLE_REMOTE_INSPECTOR"):
                verify_wpe_configuration(header)

    def test_missing_platform_setting_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            header = Path(temp) / "cmakeconfig.h"
            header.write_text("".join(
                "#define {} {}\n".format(name, value)
                for name, value in REQUIRED_WPE_CONFIGURATION.items()
                if name != "ENABLE_WPE_PLATFORM_WAYLAND"
            ))
            with self.assertRaisesRegex(ValueError, "ENABLE_WPE_PLATFORM_WAYLAND"):
                verify_wpe_configuration(header)

    @unittest.skipUnless(WPE_LIBRARY.is_file(), "build WPE first")
    def test_wpe_omits_unused_wayland_egl_runtime_dependency(self):
        dynamic = subprocess.check_output(
            ["readelf", "--wide", "-d", str(WPE_LIBRARY)], text=True
        )
        self.assertIn("Shared library: [libwayland-client.so.0]", dynamic)
        self.assertNotRegex(dynamic, r"Shared library: \[libwayland-egl\.so(?:\.1)?\]")

    @unittest.skipUnless((TARGET / "usr/lib/libgcrypt.so.20.7.2").is_file(),
                         "build libgcrypt first")
    def test_libgcrypt_uses_target_armv7_instructions(self):
        inspect_elf(TARGET / "usr/lib/libgcrypt.so.20.7.2")


@unittest.skipUnless(FONTCONFIG.is_file(), "build target dependencies first")
class RuntimeBundleTests(unittest.TestCase):
    def test_sdk_polyfill_is_rejected_with_the_file_name(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "libexample.so"
            binary.write_bytes(b"ELF data GLIBC_POLYFILLS_DEBUG")
            with patch("tools.package.subprocess.check_output", return_value=""), \
                    patch("tools.package.subprocess.run"):
                with self.assertRaisesRegex(ValueError, "libexample.so.*polyfill"):
                    normalize_elf(binary)

    def test_recursive_dependencies_are_private_and_build_rpath_is_removed(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            entrypoint = directory / "fontconfig.so"
            shutil.copyfile(FONTCONFIG, entrypoint)
            bundled = bundle_libraries(TARGET, directory, [entrypoint])
            self.assertTrue({"libfreetype.so.6", "libexpat.so.1", "libpng16.so.16",
                             "libz.so.1"}.issubset(bundled))
            self.assertFalse((directory / "lib/libc.so.6").exists())
            freetype = directory / "lib/libfreetype.so.6"
            self.assertTrue(freetype.is_file())
            self.assertNotIn(str(ROOT).encode(), freetype.read_bytes())

    def test_missing_dependency_fails_without_incomplete_success(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            target = directory / "empty-target"
            target.mkdir()
            entrypoint = directory / "fontconfig.so"
            shutil.copyfile(FONTCONFIG, entrypoint)
            with self.assertRaisesRegex(ValueError, "target library is missing"):
                bundle_libraries(target, directory, [entrypoint])


@unittest.skipUnless(GXX.is_file(), "prepare the cross compiler first")
class RuntimeVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.target = self.directory / "target"
        self.library_dir = self.target / "usr/lib"
        self.library_dir.mkdir(parents=True)
        self.app = self.directory / "app"
        subprocess.run(
            [str(GXX), "-x", "c++", "-", "-o", str(self.app)],
            input=b"#include <string>\nint main(int, char** argv) { return std::string(argv[0]).size(); }\n",
            check=True,
        )
        shutil.copy2(SDK_RUNTIME / "libgcc_s.so.1", self.library_dir / "libgcc_s.so.1")

    def test_cpp_runtime_symbol_versions_are_provided(self):
        shutil.copy2(SDK_RUNTIME / "libstdc++.so.6", self.library_dir / "libstdc++.so.6")
        bundled = bundle_libraries(self.target, self.directory, [self.app])
        self.assertIn("libstdc++.so.6", bundled)
        self.assertIn("libgcc_s.so.1", bundled)

    def test_cpp_runtime_missing_symbol_versions_is_rejected(self):
        subprocess.run(
            [str(GCC), "-shared", "-fPIC", "-Wl,-soname,libstdc++.so.6",
             "-x", "c", "-", "-o", str(self.library_dir / "libstdc++.so.6")],
            input=b"void unrelated(void) {}\n",
            check=True,
        )
        with self.assertRaisesRegex(ValueError, r"libstdc\+\+\.so\.6 lacks.*GLIBCXX"):
            bundle_libraries(self.target, self.directory, [self.app])

    def test_unverified_glibc_component_is_not_bundled(self):
        executable = self.directory / "uses-anl"
        subprocess.run(
            [str(GCC), "-x", "c", "-", "-Wl,--no-as-needed", "-lanl", "-o", str(executable)],
            input=b"int main(void) { return 0; }\n",
            check=True,
        )
        with self.assertRaisesRegex(ValueError, "unverified system glibc component: libanl"):
            bundle_libraries(self.target, self.directory, [executable])

    def test_unverified_webos_egl_stub_is_not_bundled(self):
        executable = self.directory / "uses-wayland-egl"
        subprocess.run(
            [str(GCC), "-x", "c", "-", "-Wl,--no-as-needed", "-lwayland-egl",
             "-o", str(executable)],
            input=b"int main(void) { return 0; }\n",
            check=True,
        )
        with self.assertRaisesRegex(ValueError, "unverified system graphics library: libwayland-egl"):
            bundle_libraries(self.target, self.directory, [executable])


if __name__ == "__main__":
    unittest.main()
