#!/usr/bin/env python
"""List Wayland globals without creating a surface (Python 2.7 or 3)."""

from __future__ import print_function

import os
import socket
import struct
import sys


def _read_exact(sock, size):
    chunks = []
    while size:
        chunk = sock.recv(size)
        if not chunk:
            raise ValueError("Wayland connection closed before sync completed")
        chunks.append(chunk)
        size -= len(chunk)
    return b"".join(chunks)


def read_globals(sock):
    # wl_display.get_registry(new_id=2) followed by sync(new_id=3).
    sock.sendall(struct.pack("=IIIIII", 1, (12 << 16) | 1, 2,
                            1, 12 << 16, 3))
    globals_by_id = {}
    for _ in range(4096):
        object_id, header = struct.unpack("=II", _read_exact(sock, 8))
        size, opcode = header >> 16, header & 0xffff
        if size < 8 or size % 4:
            raise ValueError("invalid Wayland message size")
        body = _read_exact(sock, size - 8)

        if object_id == 1 and opcode == 0:
            raise ValueError("Wayland compositor reported a protocol error")
        if object_id == 3 and opcode == 0:
            if len(body) != 4:
                raise ValueError("invalid Wayland sync response")
            return sorted(globals_by_id.items())
        if object_id != 2:
            continue
        if opcode == 1:
            if len(body) != 4:
                raise ValueError("invalid Wayland global removal")
            globals_by_id.pop(struct.unpack("=I", body)[0], None)
            continue
        if opcode != 0:
            continue
        if len(body) < 12:
            raise ValueError("invalid Wayland global")
        name, string_size = struct.unpack_from("=II", body)
        padded_size = (string_size + 3) & ~3
        if not string_size or len(body) != 12 + padded_size:
            raise ValueError("invalid Wayland interface name")
        raw_name = body[8:8 + string_size]
        if raw_name[-1:] != b"\0":
            raise ValueError("unterminated Wayland interface name")
        interface = raw_name[:-1].decode("utf-8")
        version = struct.unpack_from("=I", body, 8 + padded_size)[0]
        globals_by_id[name] = (interface, version)
    raise ValueError("too many Wayland messages before sync")


def main(argv):
    if len(argv) > 2:
        raise ValueError("usage: wayland_globals.py [socket-path]")
    if len(argv) == 2:
        path = argv[1]
    else:
        runtime = os.environ.get("XDG_RUNTIME_DIR")
        display = os.environ.get("WAYLAND_DISPLAY", "wayland-0")
        if os.path.isabs(display):
            path = display
        elif runtime:
            path = os.path.join(runtime, display)
        else:
            raise ValueError("set XDG_RUNTIME_DIR or pass a socket path")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        sock.settimeout(5)
        sock.connect(path)
        for name, (interface, version) in read_globals(sock):
            print("{} {} {}".format(name, interface, version))
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except (IOError, OSError, ValueError) as error:
        print("Wayland probe failed: {}".format(error), file=sys.stderr)
        sys.exit(1)
