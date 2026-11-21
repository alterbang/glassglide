import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps/sdk/arm-webos-linux-gnueabi_sdk-buildroot"
SYSROOT = SDK / "arm-webos-linux-gnueabi/sysroot"
SCANNER = SDK / "bin/wayland-scanner"
PROTOCOL = SYSROOT / "usr/share/wayland-webos/webos-shell.xml"


def message(object_id, opcode, body=b""):
    return struct.pack("=II", object_id, ((len(body) + 8) << 16) | opcode) + body


def string(value):
    value = value.encode("ascii") + b"\0"
    return struct.pack("=I", len(value)) + value + b"\0" * (-len(value) % 4)


def read_string(body, offset=0):
    length = struct.unpack_from("=I", body, offset)[0]
    start = offset + 4
    return body[start:start + length - 1].decode("ascii"), start + (length + 3) // 4 * 4


def read_exact(connection, size):
    data = b""
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            return None
        data += part
    return data


class FakeCompositor:
    def __init__(self, path, mode):
        self.mode = mode
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.bind(str(path))
        self.socket.listen(1)
        self.socket.settimeout(8)
        self.objects = {1: "wl_display"}
        self.registries = []
        self.shell_surface = None
        self.webos_surface = None
        self.events_sent = False
        self.requests = []
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()

    def run(self):
        try:
            connection, _ = self.socket.accept()
            with connection:
                connection.settimeout(8)
                while True:
                    header = read_exact(connection, 8)
                    if header is None:
                        break
                    object_id, encoded = struct.unpack("=II", header)
                    size, opcode = encoded >> 16, encoded & 0xFFFF
                    if size < 8 or size % 4:
                        raise ValueError("invalid Wayland request")
                    body = read_exact(connection, size - 8)
                    if body is None:
                        raise ValueError("truncated Wayland request")
                    self.handle(connection, object_id, opcode, body)
        except Exception as error:
            self.error = error
        finally:
            self.socket.close()

    def handle(self, connection, object_id, opcode, body):
        interface = self.objects[object_id]
        if interface == "wl_display" and opcode == 1:
            registry = struct.unpack("=I", body)[0]
            self.objects[registry] = "wl_registry"
            self.registries.append(registry)
            globals_ = [(1, "wl_compositor"), (2, "wl_shell")]
            if self.mode != "missing":
                globals_.append((3, "wl_webos_shell"))
            if self.mode == "xdg":
                globals_.append((4, "xdg_wm_base"))
            for name, name_string in globals_:
                payload = struct.pack("=I", name) + string(name_string) + struct.pack("=I", 1)
                connection.sendall(message(registry, 0, payload))
            if self.mode == "disappeared" and len(self.registries) == 2:
                connection.sendall(message(registry, 1, struct.pack("=I", 3)))
        elif interface == "wl_display" and opcode == 0:
            callback = struct.unpack("=I", body)[0]
            if self.shell_surface and self.webos_surface and not self.events_sent:
                self.events_sent = True
                if self.mode == "removed":
                    connection.sendall(message(self.registries[-1], 1, struct.pack("=I", 3)))
                else:
                    connection.sendall(message(self.shell_surface, 0, struct.pack("=I", 77)))
                    connection.sendall(message(self.shell_surface, 1, struct.pack("=Iii", 0, 1920, 1080)))
                    connection.sendall(message(self.webos_surface, 0, struct.pack("=I", 3)))
                    connection.sendall(message(self.webos_surface, 2))
            connection.sendall(message(callback, 0, struct.pack("=I", 1)))
        elif interface == "wl_registry" and opcode == 0:
            _, = struct.unpack_from("=I", body)
            name, offset = read_string(body, 4)
            version, new_id = struct.unpack_from("=II", body, offset)
            self.objects[new_id] = name
            self.requests.append(("bind", name, version))
        elif interface == "wl_compositor" and opcode == 0:
            self.objects[struct.unpack("=I", body)[0]] = "wl_surface"
        elif interface == "wl_shell" and opcode == 0:
            self.shell_surface, _ = struct.unpack("=II", body)
            self.objects[self.shell_surface] = "wl_shell_surface"
        elif interface == "wl_webos_shell" and opcode == 1:
            self.webos_surface, _ = struct.unpack("=II", body)
            self.objects[self.webos_surface] = "wl_webos_shell_surface"
        elif interface == "wl_shell_surface" and opcode == 0:
            self.requests.append(("pong", struct.unpack("=I", body)[0]))
        elif interface == "wl_shell_surface" and opcode == 5:
            self.requests.append(("fullscreen",))
        elif interface == "wl_shell_surface" and opcode == 8:
            self.requests.append(("title", read_string(body)[0]))
        elif interface == "wl_webos_shell_surface" and opcode == 1:
            self.requests.append(("state", struct.unpack("=I", body)[0]))
        elif interface == "wl_webos_shell_surface" and opcode == 2:
            key, offset = read_string(body)
            value, _ = read_string(body, offset)
            self.requests.append(("property", key, value))
        elif interface == "wl_webos_shell_surface" and opcode == 3:
            self.requests.append(("key_mask", struct.unpack("=I", body)[0]))
        elif interface == "wl_surface" and opcode == 0:
            pass
        else:
            raise ValueError("unexpected request {} opcode {}".format(interface, opcode))


@unittest.skipUnless(SCANNER.is_file() and PROTOCOL.is_file(), "prepare the webOS SDK first")
class WebosShellTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        protocol_source = directory / "webos-protocol.c"
        protocol_object = directory / "webos-protocol.o"
        cls.harness = directory / "shell-harness"
        include_path = directory / "include"
        include_path.mkdir()
        for header in ("wayland-client.h", "wayland-client-core.h",
                       "wayland-client-protocol.h", "wayland-util.h",
                       "wayland-version.h", "wayland-webos-shell-client-protocol.h"):
            shutil.copyfile(SYSROOT / "usr/include" / header, include_path / header)
        include = str(include_path)
        subprocess.run([str(SCANNER), "private-code", str(PROTOCOL), str(protocol_source)], check=True)
        subprocess.run(["cc", "-I", include, "-c", str(protocol_source), "-o", str(protocol_object)], check=True)
        subprocess.run([
            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", include,
            str(ROOT / "src/webos_shell.cpp"), str(ROOT / "tests/webos_shell_harness.cpp"),
            str(protocol_object), "-l:libwayland-client.so.0", "-o", str(cls.harness)
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mode(self, mode):
        with tempfile.TemporaryDirectory() as runtime:
            compositor = FakeCompositor(Path(runtime) / "wayland-0", mode)
            compositor.start()
            env = dict(os.environ, XDG_RUNTIME_DIR=runtime, WAYLAND_DISPLAY="wayland-0")
            process = subprocess.run([str(self.harness), mode], env=env, capture_output=True, timeout=10)
            compositor.thread.join(timeout=10)
            self.assertFalse(compositor.thread.is_alive(), "fake compositor did not finish")
            self.assertIsNone(compositor.error, str(compositor.error))
            self.assertEqual(process.returncode, 0, process.stderr.decode(errors="replace"))
            return compositor.requests

    def test_surface_roles_fullscreen_and_events(self):
        requests = self.run_mode("ready")
        self.assertIn(("bind", "wl_shell", 1), requests)
        self.assertIn(("bind", "wl_webos_shell", 1), requests)
        self.assertIn(("property", "appId", "org.glassglide.browser"), requests)
        self.assertIn(("key_mask", 0xFFFFFFFE), requests)
        self.assertIn(("state", 3), requests)
        self.assertIn(("title", "GlassGlide"), requests)
        self.assertIn(("fullscreen",), requests)
        self.assertIn(("pong", 77), requests)

    def test_missing_webos_shell_is_rejected(self):
        self.run_mode("missing")

    def test_shell_removed_during_registry_sync_is_rejected(self):
        requests = self.run_mode("disappeared")
        self.assertNotIn(("fullscreen",), requests)

    def test_xdg_shell_is_rejected_before_assigning_a_second_role(self):
        requests = self.run_mode("xdg")
        self.assertNotIn(("fullscreen",), requests)

    def test_removal_of_required_global_is_reported(self):
        self.run_mode("removed")


if __name__ == "__main__":
    unittest.main()
