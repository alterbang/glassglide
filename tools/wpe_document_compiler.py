#!/usr/bin/env python3
"""Use a private Clang for the memory-heavy WebCore Document target."""

import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
CLANG_ROOT = ROOT / ".deps/clang19/usr"
CLANG = CLANG_ROOT / "bin/clang++-19"
GCC_ONLY_FLAGS = frozenset({
    "-fcoroutines",
    "-Wno-dangling-pointer",
    "-Wno-stringop-overread",
    "-Wno-stringop-overflow",
    "-Wno-subobject-linkage",
    "-Wno-maybe-uninitialized",
    "-Winvalid-pch",
})


def clang_library_path(machine):
    triplets = {
        "aarch64": "aarch64-linux-gnu",
        "arm64": "aarch64-linux-gnu",
        "x86_64": "x86_64-linux-gnu",
        "amd64": "x86_64-linux-gnu",
    }
    if machine not in triplets:
        raise ValueError("unsupported host architecture: " + machine)
    return CLANG_ROOT / "lib" / triplets[machine]


def clang_arguments(arguments, sdk=SDK):
    if not arguments or not any(arg.startswith("--sysroot=") for arg in arguments):
        raise ValueError("missing cross-compiler command or sysroot")

    args = []
    for arg in arguments[1:]:
        if arg in GCC_ONLY_FLAGS or arg.startswith("--param=ggc-"):
            continue
        if arg == "-ftrack-macro-expansion=0":
            continue
        args.append("-ferror-limit=20" if arg == "-fmax-errors=20" else arg)

    includes = [index for index, arg in enumerate(args) if arg == "-include"]
    if len(includes) != 1 or includes[0] + 1 >= len(args):
        raise ValueError("expected exactly one generated precompiled header")
    header_index = includes[0]
    if not args[header_index + 1].endswith("/cmake_pch.hxx"):
        raise ValueError("unexpected precompiled header")
    generating_pch = any(
        arg == "-x" and index + 1 < len(args) and args[index + 1] == "c++-header"
        for index, arg in enumerate(args)
    )
    if generating_pch:
        header = args[header_index + 1]
        inputs = [index for index, arg in enumerate(args) if arg == header + ".cxx"]
        if len(inputs) != 1:
            raise ValueError("unexpected precompiled header input")
        args[inputs[0]] = header
        del args[header_index:header_index + 2]
    else:
        args[header_index] = "-include-pch"
        args[header_index + 1] += ".gch"

    return [
        str(CLANG),
        "--target=arm-webos-linux-gnueabi",
        "--gcc-toolchain=" + str(sdk),
        "-mcpu=cortex-a9",
        "-mfpu=neon-fp16",
        "-mfloat-abi=softfp",
        *args,
    ]


def main():
    try:
        args = clang_arguments(sys.argv[1:])
        library_path = str(clang_library_path(os.uname().machine))
    except ValueError as error:
        print("GlassGlide Document compiler: {}".format(error), file=sys.stderr)
        return 1
    environment = dict(os.environ)
    if environment.get("LD_LIBRARY_PATH"):
        library_path += os.pathsep + environment["LD_LIBRARY_PATH"]
    environment["LD_LIBRARY_PATH"] = library_path
    os.execve(CLANG, args, environment)


if __name__ == "__main__":
    sys.exit(main())
