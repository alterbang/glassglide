import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.wpe_document_compiler import clang_arguments, main as document_compiler_main


ROOT = Path(__file__).resolve().parents[1]
WPE_BUILD = ROOT / ".deps/buildroot-output/build/wpewebkit-2.54.2/buildroot-build"
NINJA = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/bin/ninja"


class BuildWpeTests(unittest.TestCase):
    def test_build_options_are_single_line_and_limit_parallelism(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "tools").mkdir()
            (root / ".deps").mkdir()
            (root / ".deps/.sdk-ready").touch()
            (root / ".deps/.buildroot-ready").touch()
            (root / ".deps/.clang-ready").touch()
            specs = root / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/lib/gcc/arm-webos-linux-gnueabi/16.2.0/specs"
            specs.parent.mkdir(parents=True)
            (root / ".deps/gcc-low-memory.specs").write_text("prepared specs\n", encoding="ascii")
            shutil.copy2(root / ".deps/gcc-low-memory.specs", specs)
            script = root / "tools/build-wpe.sh"
            shutil.copy2(ROOT / "tools/build-wpe.sh", script)
            shutil.copy2(ROOT / "tools/host-platform.sh", root / "tools/host-platform.sh")
            bin_dir = root / "bin"
            bin_dir.mkdir()
            make = bin_dir / "make"
            make.write_text(
                "#!/usr/bin/env python3\n"
                "import json, os, sys\n"
                "with open(os.environ['MAKE_LOG'], 'a') as log:\n"
                "    log.write(json.dumps({'args': sys.argv[1:], "
                "'ccache': os.environ.get('WK_USE_CCACHE')}) + '\\n')\n",
                encoding="ascii",
            )
            make.chmod(0o755)
            log = root / "make.log"
            env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"],
                       MAKE_LOG=str(log))
            subprocess.run([str(script)], env=env, check=True)
            invocations = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertTrue(all(call["ccache"] == "NO" for call in invocations))
            build = invocations[-1]["args"]
            self.assertIn("NINJA_OPTS=-j1", build)
            self.assertIn("PARALLEL_JOBS=1", build)
            gcrypt = next(arg for arg in build if arg.startswith("LIBGCRYPT_CONF_OPTS="))
            self.assertIn("--disable-arm-crypto-support", gcrypt)
            self.assertIn("--disable-asm", gcrypt)
            self.assertIn("WPEWEBKIT_SUPPORTS_IN_SOURCE_BUILD=NO", build)
            options = next(arg for arg in build if arg.startswith("WPEWEBKIT_CONF_OPTS="))
            self.assertNotIn("\n", options)
            self.assertIn("-DENABLE_WPE_LEGACY_API=OFF", options)
            self.assertIn("-DENABLE_BUBBLEWRAP_SANDBOX=OFF", options)
            self.assertIn("-DUSE_GSTREAMER=OFF", options)
            self.assertIn("-DCMAKE_SHARED_LINKER_FLAGS=-tno-glibc-polyfill", options)
            self.assertIn("-DGLASSGLIDE_DOCUMENT_LAUNCHER=", options)

            log.unlink()
            env["GLASSGLIDE_BUILD_JOBS"] = "2"
            subprocess.run([str(script)], env=env, check=True)
            build = json.loads(log.read_text().splitlines()[-1])["args"]
            self.assertIn("NINJA_OPTS=-j2", build)
            self.assertIn("PARALLEL_JOBS=2", build)

            env["GLASSGLIDE_BUILD_JOBS"] = "3"
            result = subprocess.run([str(script)], env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must be 1 or 2", result.stderr)

    def test_build_rejects_missing_or_changed_gcc_specs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "tools").mkdir()
            (root / ".deps").mkdir()
            for marker in (".sdk-ready", ".buildroot-ready", ".clang-ready"):
                (root / ".deps" / marker).touch()
            script = root / "tools/build-wpe.sh"
            shutil.copy2(ROOT / "tools/build-wpe.sh", script)
            shutil.copy2(ROOT / "tools/host-platform.sh", root / "tools/host-platform.sh")
            (root / ".deps/gcc-low-memory.specs").write_text("prepared specs\n", encoding="ascii")
            specs = root / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/lib/gcc/arm-webos-linux-gnueabi/16.2.0/specs"
            specs.parent.mkdir(parents=True)
            for content in (None, "unexpected specs\n"):
                if content is not None:
                    specs.write_text(content, encoding="ascii")
                result = subprocess.run([str(script)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("GCC specs are missing or changed", result.stderr)

    def test_low_memory_specs_preserve_sdk_linker_options(self):
        baseline = "*cc1plus:\noriginal frontend\n\n*libgcc:\nrequired linker options\n\n"
        result = subprocess.run(
            ["awk", "-f", str(ROOT / "tools/wpe_gcc_specs.awk")],
            input=baseline, capture_output=True, text=True, check=True,
        )
        self.assertIn("original frontend --param=ggc-min-expand=10", result.stdout)
        self.assertIn("*libgcc:\nrequired linker options\n", result.stdout)

    @unittest.skipUnless((ROOT / ".deps/.sdk-ready").is_file(), "prepare the SDK first")
    def test_private_gcc_applies_memory_options(self):
        compiler = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot/bin/arm-webos-linux-gnueabi-g++"
        result = subprocess.run(
            [str(compiler), "-x", "c++", "-", "-fsyntax-only", "-###"],
            input="int example;\n", capture_output=True, text=True, check=True,
        )
        self.assertIn("--param=ggc-min-expand=10", result.stderr)
        self.assertIn("--param=ggc-min-heapsize=8192", result.stderr)

    @unittest.skipUnless(
        (WPE_BUILD / "Source/WebCore/WebCoreBundledSources.txt").is_file(),
        "configure WPE first",
    )
    def test_document_is_compiled_outside_unified_bundles(self):
        bundled = WPE_BUILD / "Source/WebCore/WebCoreBundledSources.txt"
        self.assertNotIn("dom/Document.cpp", bundled.read_text().splitlines())
        target = "Source/WebCore/CMakeFiles/WebCoreDocument.dir/dom/Document.cpp.o"
        with (WPE_BUILD / "build.ninja").open() as ninja:
            self.assertTrue(any(
                line.startswith("build " + target + ":")
                for line in ninja
            ))
        commands = subprocess.check_output([
            str(NINJA), "-C", str(WPE_BUILD), "-t", "commands",
            target,
        ], text=True).splitlines()
        command = commands[-1]
        optimizations = [
            option for option in shlex.split(command)
            if re.fullmatch(r"-O(?:[0-3sgz]|fast)", option)
        ]
        self.assertEqual(optimizations[-1], "-O0")
        self.assertIn("-DRELEASE_WITHOUT_OPTIMIZATIONS=ON", command)
        self.assertIn("wpe_document_compiler.py", command)
        self.assertIn("cmake_pch.hxx", command)
        self.assertTrue(any("WebCoreDocument.dir/cmake_pch.hxx.gch" in line for line in commands))
        with (WPE_BUILD / "build.ninja").open() as ninja:
            link = next(line for line in ninja
                        if line.startswith("  LINK_LIBRARIES = ")
                        and "WebCoreDocument.dir/" in line)
        self.assertRegex(link, r"WebCoreDocument\.dir/(?:\./)?dom/Document\.cpp\.o")
        self.assertNotRegex(link, r"WebCoreDocument\.dir/\S+cmake_pch\.hxx\.gch")

    @unittest.skipUnless(
        (WPE_BUILD / "Source/WebCore/WebCoreBundledSources.txt").is_file(),
        "configure WPE first",
    )
    def test_heavy_html_sources_are_compiled_separately(self):
        bundled = (WPE_BUILD / "Source/WebCore/WebCoreBundledSources.txt").read_text().splitlines()
        ninja = (WPE_BUILD / "build.ninja").read_text()
        for name in ("HTMLPlugInElement", "HTMLProgressElement", "HTMLSelectElement"):
            self.assertNotIn("html/{}.cpp".format(name), bundled)
            self.assertIn("WebCore.dir/html/{}.cpp.o:".format(name), ninja)

    def test_document_launcher_translates_gcc_pch_commands(self):
        common = ["arm-g++", "--sysroot=/target", "-fmax-errors=20",
                  "-Wno-dangling-pointer", "-include", "/build/cmake_pch.hxx"]
        create = clang_arguments(common + ["-x", "c++-header", "-c", "/build/cmake_pch.hxx.cxx"])
        self.assertNotIn("-include", create)
        self.assertNotIn("-include-pch", create)
        self.assertEqual(create[-1], "/build/cmake_pch.hxx")
        self.assertIn("-ferror-limit=20", create)
        self.assertNotIn("-Wno-dangling-pointer", create)
        compile_args = clang_arguments(common + ["-c", "Document.cpp"])
        include = compile_args.index("-include-pch")
        self.assertEqual(compile_args[include + 1], "/build/cmake_pch.hxx.gch")
        self.assertIn("-mfloat-abi=softfp", compile_args)

    def test_document_launcher_rejects_unexpected_pch_or_missing_sysroot(self):
        with self.assertRaisesRegex(ValueError, "sysroot"):
            clang_arguments(["arm-g++", "-include", "/build/cmake_pch.hxx"])
        with self.assertRaisesRegex(ValueError, "unexpected precompiled header"):
            clang_arguments(["arm-g++", "--sysroot=/target", "-include", "/other.h"])

    def test_document_launcher_uses_host_clang_libraries(self):
        args = ["wpe_document_compiler.py", "arm-g++", "--sysroot=/target",
                "-include", "/build/cmake_pch.hxx", "-c", "Document.cpp"]
        for machine, triplet in (("aarch64", "aarch64-linux-gnu"),
                                 ("x86_64", "x86_64-linux-gnu")):
            with self.subTest(machine=machine), \
                    patch("sys.argv", args), \
                    patch("os.uname") as uname, \
                    patch("os.execve") as execute, \
                    patch.dict(os.environ, {"LD_LIBRARY_PATH": "/existing"}):
                uname.return_value.machine = machine
                document_compiler_main()
                environment = execute.call_args.args[2]
                self.assertEqual(
                    environment["LD_LIBRARY_PATH"],
                    str(ROOT / ".deps/clang19/usr/lib" / triplet) + ":/existing",
                )

    @unittest.skipUnless((ROOT / ".deps/.clang-ready").is_file(), "prepare Clang first")
    def test_document_pch_can_be_rebuilt_without_self_import(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            header = directory / "cmake_pch.hxx"
            dummy = directory / "cmake_pch.hxx.cxx"
            source = directory / "document.cpp"
            pch = directory / "cmake_pch.hxx.gch"
            header.write_text("constexpr int value = 42;\n", encoding="ascii")
            dummy.write_text("", encoding="ascii")
            source.write_text("static_assert(value == 42);\n", encoding="ascii")
            launcher = str(ROOT / "tools/wpe_document_compiler.py")
            sysroot = ROOT / ".deps/buildroot-output/host/arm-buildroot-linux-gnueabi/sysroot"
            common = [sys.executable, launcher, "arm-g++", "--sysroot=" + str(sysroot),
                      "-std=c++17", "-include", str(header)]
            create = common + ["-x", "c++-header", "-o", str(pch), "-c", str(dummy)]
            subprocess.run(create, check=True, capture_output=True)
            subprocess.run(create, check=True, capture_output=True)
            subprocess.run(common + ["-o", str(directory / "document.o"), "-c", str(source)],
                           check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
