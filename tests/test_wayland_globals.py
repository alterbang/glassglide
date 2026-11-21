import os
import socket
import struct
import sys
import threading
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import wayland_globals


def message(object_id, opcode, body):
    return struct.pack("=II", object_id, ((len(body) + 8) << 16) | opcode) + body


def global_message(name, interface, version):
    encoded = interface.encode("utf-8") + b"\0"
    padded = encoded + b"\0" * ((-len(encoded)) % 4)
    return message(2, 0, struct.pack("=II", name, len(encoded)) +
                   padded + struct.pack("=I", version))


class WaylandGlobalsTest(unittest.TestCase):
    def run_probe(self, response):
        client, server = socket.socketpair()
        client.settimeout(2)
        server.settimeout(2)
        requests = []

        def serve():
            try:
                requests.append(wayland_globals._read_exact(server, 24))
                for offset in range(0, len(response), 3):
                    server.sendall(response[offset:offset + 3])
            finally:
                server.close()

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            result = wayland_globals.read_globals(client)
        finally:
            client.close()
            thread.join()
        self.assertEqual(requests[0], struct.pack("=IIIIII", 1, (12 << 16) | 1,
                                                  2, 1, 12 << 16, 3))
        return result

    def test_fragmented_events_and_global_removal(self):
        response = (global_message(5, "wl_compositor", 3) +
                    global_message(7, "xdg_wm_base", 1) +
                    message(2, 1, struct.pack("=I", 7)) +
                    message(3, 0, struct.pack("=I", 42)))
        self.assertEqual(self.run_probe(response), [(5, ("wl_compositor", 3))])

    def test_truncated_sync_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "closed"):
            self.run_probe(global_message(1, "wl_shm", 1))

    def test_malformed_interface_name_is_an_error(self):
        response = message(2, 0, struct.pack("=II", 1, 4) + b"oops" +
                           struct.pack("=I", 1))
        with self.assertRaisesRegex(ValueError, "unterminated"):
            self.run_probe(response)


if __name__ == "__main__":
    unittest.main()
