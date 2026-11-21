#!/usr/bin/env python3
"""Normalize ownership and permissions in an official webOS IPK."""

import gzip
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile


# The TV's GNU tar 1.17 warns on epoch-zero entries, which breaks verification.
ARCHIVE_MTIME = 946684800  # 2000-01-01 UTC, before the TV's default clock
SHARED_DATA_DIRS = frozenset({
    "usr", "usr/palm", "usr/palm/applications", "usr/palm/packages",
})


def _normalize_tar(source, destination, omit_shared_dirs=False):
    with tarfile.open(source, "r:gz") as incoming, destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.USTAR_FORMAT) as outgoing:
                for member in incoming:
                    name = PurePosixPath(member.name)
                    if name.is_absolute() or ".." in name.parts:
                        raise ValueError("unsafe archive path: " + member.name)
                    if omit_shared_dirs and str(name) in SHARED_DATA_DIRS:
                        if not member.isdir():
                            raise ValueError("shared archive path is not a directory: " + member.name)
                        continue
                    if member.isdir():
                        member.mode = 0o755
                        content = None
                    elif member.isfile():
                        member.mode = 0o755 if member.mode & 0o111 else 0o644
                        content = incoming.extractfile(member)
                        if content is None:
                            raise ValueError("cannot read archive member: " + member.name)
                    else:
                        raise ValueError("unsupported archive member: " + member.name)
                    member.uid = member.gid = 0
                    member.uname = member.gname = "root"
                    member.mtime = ARCHIVE_MTIME
                    member.pax_headers = {}
                    outgoing.addfile(member, content)
                    if content:
                        content.close()


def _set_regular_ar_modes(path, members):
    with path.open("r+b") as archive:
        if archive.read(8) != b"!<arch>\n":
            raise ValueError("invalid IPK archive")
        archive_size = archive.seek(0, os.SEEK_END)
        archive.seek(8)
        for name in members:
            offset = archive.tell()
            header = archive.read(60)
            if len(header) != 60 or header[58:60] != b"`\n" \
                    or header[:16].rstrip().removesuffix(b"/") != name.encode("ascii"):
                raise ValueError("unexpected IPK archive header: " + name)
            size = int(header[48:58])
            end = offset + 60 + size + size % 2
            if size < 0 or end > archive_size:
                raise ValueError("invalid IPK archive member size: " + name)
            # BusyBox ar on webOS requires the regular-file type bits.
            archive.seek(offset + 40)
            archive.write(b"100644  ")
            archive.seek(end)
        if archive.tell() != archive_size:
            raise ValueError("unexpected trailing IPK archive data")


def sanitize_ipk(source, destination):
    source = Path(source).resolve(strict=True)
    destination = Path(destination).absolute()
    members = subprocess.check_output(["ar", "t", str(source)], text=True).splitlines()
    if members != ["debian-binary", "control.tar.gz", "data.tar.gz"]:
        raise ValueError("unexpected IPK archive members")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".glassglide-ipk-", dir=destination.parent) as temp:
        directory = Path(temp)
        for name in members:
            with (directory / (name + ".original")).open("wb") as output:
                subprocess.run(["ar", "p", str(source), name], stdout=output, check=True)
        binary = (directory / "debian-binary.original").read_bytes()
        if binary != b"2.0\n":
            raise ValueError("unsupported IPK format version")
        (directory / "debian-binary").write_bytes(binary)
        for name in ("control.tar.gz", "data.tar.gz"):
            _normalize_tar(directory / (name + ".original"), directory / name,
                           omit_shared_dirs=name == "data.tar.gz")
        clean = directory / "clean.ipk"
        subprocess.run(["ar", "rcD", str(clean), *members], cwd=directory, check=True)
        _set_regular_ar_modes(clean, members)
        clean.chmod(0o644)
        os.replace(clean, destination)
