#!/usr/bin/env python3
"""Check ARM binaries against the webOS 3 loader and glibc baseline."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys


@dataclass
class ElfInfo:
    needed: list[str]
    version_needs: dict[str, set[str]]
    version_definitions: set[str]


def _version_info(text):
    definitions = text.partition("Version definition section")[2].partition("Version needs section")[0]
    defined = set(re.findall(r"Name:\s+([^\s]+)", definitions))
    needs = {}
    current_library = None
    for line in text.partition("Version needs section")[2].splitlines():
        library = re.search(r"\bFile:\s+([^\s]+)", line)
        if library:
            current_library = library.group(1)
            needs.setdefault(current_library, set())
        version = re.search(r"\bName:\s+([^\s]+)", line)
        if version and current_library:
            needs[current_library].add(version.group(1))
    return needs, defined


def _number(value):
    token = value.split()[0]
    return int(token, 16 if token.startswith("0x") else 10)


def inspect_elf(path):
    path = Path(path)
    with path.open("rb") as binary:
        if binary.read(4) != b"\x7fELF":
            raise ValueError("not an ELF file")
    result = subprocess.run(
        ["readelf", "--wide", "-h", "-l", "-d", "-V", "-A", str(path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    if result.returncode:
        raise ValueError("readelf failed: " + result.stderr.strip())
    text = result.stdout
    if not re.search(r"Class:\s+ELF32\b", text) \
            or not re.search(r"Machine:\s+ARM\b", text) \
            or not re.search(r"Data:\s+2's complement, little endian", text):
        raise ValueError("expected little-endian 32-bit ARM")
    flags = re.search(r"Flags:\s+(0x[0-9a-fA-F]+)", text)
    if not flags:
        raise ValueError("ELF flags are missing")
    flag_value = int(flags.group(1), 16)
    if flag_value & 0xFF000000 != 0x05000000 or flag_value & 0x400:
        raise ValueError("expected ARM EABI5 without hard-float calling convention")

    def attribute(name):
        match = re.search(r"^\s*{}:\s*(.+?)\s*$".format(re.escape(name)),
                          text, re.MULTILINE)
        return match.group(1) if match else None

    if attribute("Tag_CPU_arch") != "v7" \
            or attribute("Tag_CPU_arch_profile") != "Application":
        raise ValueError("expected ARMv7-A architecture attributes")
    if attribute("Tag_ABI_VFP_args") not in (None, "Base AAPCS"):
        raise ValueError("expected soft-float calling convention")
    if attribute("Tag_FP_arch") not in (None, "VFPv1", "VFPv2", "VFPv3", "VFPv3-D16"):
        raise ValueError("unsupported floating-point architecture")
    if attribute("Tag_Advanced_SIMD_arch") not in (None, "NEONv1"):
        raise ValueError("unsupported SIMD architecture")

    interpreter = re.search(r"Requesting program interpreter:\s*([^\]]+)", text)
    if interpreter and interpreter.group(1) != "/lib/ld-linux.so.3":
        raise ValueError("unexpected program interpreter: " + interpreter.group(1))

    tags = dict(re.findall(
        r"\((REL|RELSZ|RELENT|JMPREL|PLTREL|PLTRELSZ)\)\s+([^\n]+)", text
    ))
    rel_size = _number(tags["RELSZ"]) if "RELSZ" in tags else 0
    plt_size = _number(tags["PLTRELSZ"]) if "PLTRELSZ" in tags else 0
    if rel_size and plt_size:
        required = ("REL", "RELENT", "JMPREL", "PLTREL")
        if any(tag not in tags for tag in required):
            raise ValueError("incomplete ARM relocation metadata")
        if _number(tags["RELENT"]) != 8 or tags["PLTREL"].strip() != "REL":
            raise ValueError("unexpected ARM relocation format")
        if _number(tags["REL"]) + rel_size != _number(tags["JMPREL"]):
            raise ValueError("DT_REL and DT_JMPREL are not adjacent for the webOS 3 loader")

    for kind, value in re.findall(r"\((RPATH|RUNPATH)\).*?\[([^\]]*)\]", text):
        if any(entry != "$ORIGIN" and (
                not entry.startswith("$ORIGIN/") or ".." in entry.split("/")
        ) for entry in value.split(":")):
            raise ValueError("non-relocatable {}: {}".format(kind, value))

    version_needs, version_definitions = _version_info(text)
    for versions in version_needs.values():
        for version in versions:
            if not version.startswith("GLIBC_"):
                continue
            match = re.fullmatch(r"GLIBC_(\d+(?:\.\d+)+)", version)
            if not match:
                raise ValueError("unsupported glibc version requirement: " + version)
            parts = tuple(int(part) for part in match.group(1).split("."))
            if parts > (2, 19):
                raise ValueError("requires {}, newer than the TV's glibc 2.19".format(version))

    needed = re.findall(r"\(NEEDED\).*?\[([^\]]+)\]", text)
    if any("/" in library for library in needed):
        raise ValueError("an ELF dependency contains a filesystem path")
    return ElfInfo(needed, version_needs, version_definitions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    failed = False
    for path in args.paths:
        try:
            inspect_elf(path)
        except (OSError, ValueError) as error:
            print("{}: {}".format(path, error), file=sys.stderr)
            failed = True
        else:
            print("{}: ARM/webOS 3 checks passed".format(path))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
