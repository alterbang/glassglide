#!/usr/bin/env python3
"""Assemble the local-only WPE prototype into a private webOS IPK."""

from collections import deque
import json
import mmap
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from .audit_elf import inspect_elf
from .ipk import sanitize_ipk


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / ".deps/buildroot-output"
SDK = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
TARGET = OUTPUT / "target"
PATCHELF = SDK / "bin/patchelf"
STRIP = SDK / "bin/arm-webos-linux-gnueabi-strip"

# Verified on the target TV. The LG graphics and webOS libraries must remain
# system-provided; the newer Wayland, GLib and C++ libraries are private.
SYSTEM_LIBRARIES = frozenset({
    "ld-linux.so.3", "libc.so.6", "libm.so.6", "libdl.so.2",
    "libpthread.so.0", "librt.so.1", "libresolv.so.2", "libutil.so.1",
    "libcrypt.so.1", "libnsl.so.1", "libEGL.so.1", "libGLESv2.so.2",
    "libwayland-webos-client.so.1",
})

UNVERIFIED_WEBOS_GRAPHICS = frozenset({
    "libwayland-egl.so", "libwayland-egl.so.1",
})

REQUIRED_WPE_CONFIGURATION = {
    "ENABLE_2022_GLIB_API": "1",
    "ENABLE_WPE_PLATFORM": "1",
    "ENABLE_WPE_PLATFORM_WAYLAND": "1",
    "ENABLE_WPE_LEGACY_API": "0",
    "ENABLE_DEVELOPER_MODE": "0",
    "ENABLE_REMOTE_INSPECTOR": "0",
    "ENABLE_BUBBLEWRAP_SANDBOX": "0",
    "USE_GSTREAMER": "0",
}


def verify_wpe_configuration(path):
    text = Path(path).read_text(encoding="utf-8")
    definitions = {}
    for name, value in re.findall(r"^#define\s+(\w+)\s+([01])\s*$", text, re.MULTILINE):
        definitions.setdefault(name, set()).add(value)
    for name, expected in REQUIRED_WPE_CONFIGURATION.items():
        if definitions.get(name) != {expected}:
            raise ValueError("unexpected WPE build configuration: " + name)


def normalize_elf(path, patchelf=PATCHELF, strip=STRIP):
    dynamic = subprocess.check_output(["readelf", "--wide", "-d", str(path)], text=True)
    if re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        subprocess.run([str(patchelf), "--set-rpath", "$ORIGIN", str(path)], check=True)
        subprocess.run([str(patchelf), "--remove-rpath", str(path)], check=True)
    subprocess.run([str(strip), "--strip-unneeded", str(path)], check=True)
    with Path(path).open("rb") as binary:
        if binary.seek(0, 2):
            with mmap.mmap(binary.fileno(), 0, access=mmap.ACCESS_READ) as contents:
                if contents.find(b"GLIBC_POLYFILLS_") != -1:
                    raise ValueError("{}: unlicensed SDK glibc polyfill detected".format(path))
    try:
        return inspect_elf(path)
    except ValueError as error:
        raise ValueError("{}: {}".format(path, error)) from error


def bundle_libraries(target, directory, entrypoints, patchelf=PATCHELF, strip=STRIP):
    target = Path(target).resolve(strict=True)
    library_dir = Path(directory) / "lib"
    library_dir.mkdir(parents=True, exist_ok=True)
    pending = deque()
    inspected = {}
    for path in entrypoints:
        info = normalize_elf(path, patchelf, strip)
        inspected[str(path)] = info
        pending.extend(info.needed)

    bundled = set()
    providers = {}
    while pending:
        name = pending.popleft()
        if name in SYSTEM_LIBRARIES or name in bundled:
            continue
        if name in (".", "..") or not re.fullmatch(r"[A-Za-z0-9._+~-]+", name):
            raise ValueError("invalid dependency name: " + name)
        if name in UNVERIFIED_WEBOS_GRAPHICS:
            raise ValueError("unverified system graphics library: " + name)
        if re.fullmatch(r"lib(?:anl|BrokenLocale|cidn|mvec|thread_db|nss_[A-Za-z0-9_]+)"
                        r"\.so(?:\.\d+)*", name):
            raise ValueError("unverified system glibc component: " + name)

        source = None
        for parent in (target / "lib", target / "usr/lib"):
            candidate = parent / name
            if candidate.is_file():
                source = candidate.resolve(strict=True)
                break
        if source is None:
            raise ValueError("target library is missing: " + name)
        if not source.is_relative_to(target):
            raise ValueError("target library escapes the build tree: " + name)
        destination = library_dir / name
        shutil.copy2(source, destination)
        info = normalize_elf(destination, patchelf, strip)
        inspected[str(destination)] = info
        providers[name] = info
        pending.extend(info.needed)
        bundled.add(name)

    for path, info in inspected.items():
        for library in ("libstdc++.so.6", "libgcc_s.so.1"):
            required = info.version_needs.get(library, set())
            if not required:
                continue
            provider = providers.get(library)
            if provider is None:
                raise ValueError("{}: missing versioned library {}".format(path, library))
            missing = required - provider.version_definitions
            if missing:
                raise ValueError("{}: {} lacks {}".format(path, library, ", ".join(sorted(missing))))
    return bundled


def _copy_file(source, destination):
    if not source.is_file():
        raise ValueError("required file is missing: " + str(source))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return destination


def create_package():
    verify_wpe_configuration(
        OUTPUT / "build/wpewebkit-2.54.2/buildroot-build/cmakeconfig.h"
    )
    legal = OUTPUT / "legal-info"
    if not (legal / "manifest.csv").is_file() or not (legal / "licenses").is_dir():
        raise ValueError("generate Buildroot notices first: tools/build-wpe.sh legal-info")
    cli = ROOT / "node_modules/.bin/ares-package"
    if not cli.is_file():
        raise ValueError("install the pinned @webos-tools/cli build dependency first")
    info = json.loads((ROOT / "package/appinfo.json").read_text(encoding="utf-8"))
    if not re.fullmatch(r"[a-z0-9][a-z0-9.-]+", info["id"]) \
            or not re.fullmatch(r"\d+\.\d+\.\d+", info["version"]):
        raise ValueError("invalid application ID or version")

    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="glassglide-package-", dir=build) as temp:
        work = Path(temp)
        app = work / "app"
        app.mkdir()
        _copy_file(ROOT / "package/appinfo.json", app / "appinfo.json")
        _copy_file(ROOT / "package/icon.png", app / "icon.png")
        launcher = _copy_file(build / "glassglide-launch", app / "glassglide-launch")
        executable = _copy_file(build / "glassglide", app / "bin/glassglide")
        helper_dir = TARGET / "usr/libexec/wpe-webkit-2.0"
        helpers = []
        for name in ("WPEWebProcess", "WPENetworkProcess"):
            helpers.append(_copy_file(helper_dir / name, app / "libexec/wpe-webkit-2.0" / name))
        gpu_process = helper_dir / "WPEGPUProcess"
        if gpu_process.is_file():
            helpers.append(_copy_file(gpu_process, app / "libexec/wpe-webkit-2.0/WPEGPUProcess"))
        bundle = _copy_file(
            TARGET / "usr/lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so",
            app / "lib/wpe-webkit-2.0/injected-bundle/libWPEInjectedBundle.so",
        )
        (app / "lib/gio/modules").mkdir(parents=True)
        shutil.copytree(legal / "licenses", app / "licenses")
        _copy_file(legal / "manifest.csv", app / "licenses/manifest.csv")
        _copy_file(legal / "README", app / "licenses/README.buildroot")

        bundled = bundle_libraries(TARGET, app, [launcher, executable, bundle, *helpers])
        subprocess.run([str(cli), "--outdir", str(work), str(app)], check=True)
        raw_ipk = work / "{}_{}_arm.ipk".format(info["id"], info["version"])
        if not raw_ipk.is_file():
            raise ValueError("webOS packager did not produce an ARM IPK")
        destination = ROOT / "dist" / raw_ipk.name
        destination.parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".glassglide-release-", dir=destination.parent) as release:
            candidate = Path(release) / "verified.ipk"
            sanitize_ipk(raw_ipk, candidate)
            details = subprocess.check_output([str(cli), "--info-detail", str(candidate)], text=True)
            for field, value in (("Package", info["id"]), ("Version", info["version"]),
                                 ("Architecture", "arm")):
                if not re.search(r"^{}:\s*{}\s*$".format(field, re.escape(value)),
                                 details, re.MULTILINE):
                    raise ValueError("invalid IPK {} metadata".format(field))
            candidate.replace(destination)
        print("Created {} with {} private libraries".format(destination, len(bundled)))


def main():
    try:
        create_package()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("GlassGlide packaging failed: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
