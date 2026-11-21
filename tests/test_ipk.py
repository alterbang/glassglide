import io
from pathlib import Path
import stat
import subprocess
import tarfile
import tempfile
import unittest

from tools.ipk import sanitize_ipk


def tar_member(archive, name, mode, kind=tarfile.REGTYPE):
    member = tarfile.TarInfo(name)
    member.type = kind
    member.uid = member.gid = 1000
    member.uname = "host-user"
    member.gname = "host-group"
    member.mode = mode
    data = b"fixture" if kind == tarfile.REGTYPE else b""
    member.size = len(data)
    archive.addfile(member, io.BytesIO(data) if data else None)


class IpkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "debian-binary").write_bytes(b"2.0\n")
        with tarfile.open(self.root / "control.tar.gz", "w:gz") as archive:
            tar_member(archive, "control", 0o666)

    def make_ipk(self, name, kind=tarfile.REGTYPE):
        with tarfile.open(self.root / "data.tar.gz", "w:gz") as archive:
            tar_member(archive, "usr", 0o777, tarfile.DIRTYPE)
            tar_member(archive, "usr/palm", 0o777, tarfile.DIRTYPE)
            tar_member(archive, "usr/palm/applications", 0o777, tarfile.DIRTYPE)
            tar_member(archive, "usr/palm/packages", 0o777, tarfile.DIRTYPE)
            tar_member(archive, "usr/palm/applications/org.example.app", 0o777, tarfile.DIRTYPE)
            tar_member(archive, name, 0o777, kind)
            tar_member(archive, "usr/palm/applications/config.json", 0o666)
        source = self.root / "input.ipk"
        subprocess.run(["ar", "rc", str(source), "debian-binary", "control.tar.gz", "data.tar.gz"],
                       cwd=self.root, check=True)
        return source

    def test_owner_permissions_and_executable_are_normalized(self):
        source = self.make_ipk("usr/palm/applications/launcher")
        output = self.root / "output.ipk"
        sanitize_ipk(source, output)
        compressed = subprocess.check_output(["ar", "p", str(output), "data.tar.gz"])
        with tarfile.open(fileobj=io.BytesIO(compressed), mode="r:gz") as archive:
            members = {member.name: member for member in archive}
        for member in members.values():
            self.assertEqual((member.uid, member.gid, member.uname, member.gname),
                             (0, 0, "root", "root"))
        self.assertEqual(members["usr/palm/applications/org.example.app"].mode, 0o755)
        self.assertEqual(members["usr/palm/applications/launcher"].mode, 0o755)
        self.assertEqual(members["usr/palm/applications/config.json"].mode, 0o644)

    def test_extracting_does_not_change_shared_directory_modes(self):
        source = self.make_ipk("usr/palm/applications/launcher")
        output = self.root / "output.ipk"
        sanitize_ipk(source, output)
        compressed = subprocess.check_output(["ar", "p", str(output), "data.tar.gz"])
        target = self.root / "installed"
        for name in ("usr", "usr/palm", "usr/palm/applications", "usr/palm/packages"):
            directory = target / name
            directory.mkdir(parents=True, exist_ok=True)
            directory.chmod(0o777)
        subprocess.run(["tar", "-xz", "-C", str(target)], input=compressed, check=True)
        for name in ("usr", "usr/palm", "usr/palm/applications", "usr/palm/packages"):
            self.assertEqual((target / name).stat().st_mode & 0o777, 0o777)
        self.assertEqual(
            (target / "usr/palm/applications/org.example.app").stat().st_mode & 0o777,
            0o755,
        )

    def test_timestamps_are_reproducible_and_suitable_for_legacy_tar(self):
        source = self.make_ipk("usr/palm/applications/launcher")
        first = self.root / "first.ipk"
        second = self.root / "second.ipk"
        sanitize_ipk(source, first)
        sanitize_ipk(source, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        for name in ("control.tar.gz", "data.tar.gz"):
            compressed = subprocess.check_output(["ar", "p", str(first), name])
            with tarfile.open(fileobj=io.BytesIO(compressed), mode="r:gz") as archive:
                for member in archive:
                    self.assertGreaterEqual(member.mtime, 315532800)  # 1980-01-01
                    self.assertLess(member.mtime, 1483228800)  # 2017-01-01

    def test_ar_members_have_regular_file_types(self):
        source = self.make_ipk("usr/palm/applications/launcher")
        output = self.root / "output.ipk"
        sanitize_ipk(source, output)
        with output.open("rb") as archive:
            self.assertEqual(archive.read(8), b"!<arch>\n")
            for name in (b"debian-binary", b"control.tar.gz", b"data.tar.gz"):
                header = archive.read(60)
                self.assertEqual(header[:16].rstrip().removesuffix(b"/"), name)
                self.assertTrue(stat.S_ISREG(int(header[40:48], 8)))
                size = int(header[48:58])
                archive.seek(size + size % 2, 1)

    def test_long_paths_use_ustar_prefix_without_pax_headers(self):
        name = "usr/palm/applications/org.example.app/" + "nested/" * 10 + "bundle.so"
        self.assertGreater(len(name), 100)
        source = self.make_ipk(name)
        output = self.root / "output.ipk"
        sanitize_ipk(source, output)
        compressed = subprocess.check_output(["ar", "p", str(output), "data.tar.gz"])
        with tarfile.open(fileobj=io.BytesIO(compressed), mode="r:gz") as archive:
            member = archive.getmember(name)
            self.assertEqual(member.pax_headers, {})
            self.assertEqual(archive.extractfile(member).read(), b"fixture")

    def test_path_unrepresentable_in_ustar_is_rejected(self):
        name = "usr/palm/applications/" + "a" * 101
        source = self.make_ipk(name)
        output = self.root / "output.ipk"
        with self.assertRaisesRegex(ValueError, "name is too long"):
            sanitize_ipk(source, output)
        self.assertFalse(output.exists())

    def test_path_traversal_is_rejected(self):
        source = self.make_ipk("../outside")
        with self.assertRaisesRegex(ValueError, "unsafe archive path"):
            sanitize_ipk(source, self.root / "output.ipk")
        self.assertFalse((self.root / "output.ipk").exists())

    def test_links_are_rejected(self):
        source = self.make_ipk("usr/palm/applications/link", tarfile.SYMTYPE)
        with self.assertRaisesRegex(ValueError, "unsupported archive member"):
            sanitize_ipk(source, self.root / "output.ipk")
        self.assertFalse((self.root / "output.ipk").exists())


if __name__ == "__main__":
    unittest.main()
