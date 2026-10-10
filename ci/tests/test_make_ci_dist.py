import hashlib
import importlib.util
import io
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "make_ci_dist", Path(__file__).resolve().parents[1] / "make-ci-dist.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# arm64-v8a -> 64-bit EM_AARCH64 (0xb7); armeabi-v7a -> 32-bit EM_ARM (0x28).
ABI_ELF = {"arm64-v8a": (True, 0xB7), "armeabi-v7a": (False, 0x28)}


def _note(build_id: bytes) -> bytes:
    name = b"GNU\x00"
    desc = build_id + b"\x00" * ((-len(build_id)) % 4)
    return struct.pack("<III", len(name), len(build_id), 3) + name + desc


def build_elf(class64: bool, machine: int, build_id: bytes) -> bytes:
    note = _note(build_id)
    ehsize, ei_class, shentsize = (64, 2, 64) if class64 else (52, 1, 40)
    note_off = ehsize
    shoff = note_off + len(note)
    data = bytearray(shoff + shentsize)
    data[0:4] = b"\x7fELF"
    data[4] = ei_class
    data[5] = 1  # little-endian
    data[6] = 1  # EI_VERSION
    struct.pack_into("<H", data, 16, 3)        # e_type = ET_DYN
    struct.pack_into("<H", data, 18, machine)  # e_machine
    if class64:
        struct.pack_into("<Q", data, 40, shoff)
        struct.pack_into("<H", data, 58, shentsize)
        struct.pack_into("<H", data, 60, 1)    # e_shnum
        struct.pack_into("<IIQQQQIIQQ", data, shoff,
                         0, 7, 0, 0, note_off, len(note), 0, 0, 0, 0)
    else:
        struct.pack_into("<I", data, 32, shoff)
        struct.pack_into("<H", data, 46, shentsize)
        struct.pack_into("<H", data, 48, 1)    # e_shnum
        struct.pack_into("<IIIIIIIIII", data, shoff,
                         0, 7, 0, 0, note_off, len(note), 0, 0, 0, 0)
    return bytes(data)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class MakeCiDistTest(unittest.TestCase):
    VERSION = "1.2.3"
    COMMIT = "a" * 40

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build_ids = {abi: bytes([i + 1]) * 16
                          for i, abi in enumerate(ABI_ELF)}

    def _bridge_so(self, abi):
        class64, machine = ABI_ELF[abi]
        return build_elf(class64, machine, self.build_ids[abi])

    def _write_aar(self, distribution):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("AndroidManifest.xml", b"<manifest/>")
            z.writestr("classes.jar", b"PK\x03\x04stub")
            for abi in ABI_ELF:
                z.writestr(f"jni/{abi}/{mod.BRIDGE_LIB}", self._bridge_so(abi))
                if distribution == "full":
                    z.writestr(f"jni/{abi}/{mod.DECODER_LIB}",
                               f"decoder-{abi}".encode())
            if distribution == "full":
                z.writestr("assets/avs3a/model.bin", b"model-bytes")
        aar = self.root / f"sdk-{distribution}-release.aar"
        aar.write_bytes(buf.getvalue())
        return aar

    def _write_maven(self, artifact_id, aar_path):
        version_dir = (self.root / "ci-maven" / "com" / "inlz" / "avs3a"
                       / artifact_id / self.VERSION)
        version_dir.mkdir(parents=True)
        prefix = f"{artifact_id}-{self.VERSION}"
        (version_dir / f"{prefix}.aar").write_bytes(aar_path.read_bytes())
        (version_dir / f"{prefix}.pom").write_text(
            f"<project><groupId>com.inlz.avs3a</groupId>"
            f"<artifactId>{artifact_id}</artifactId>"
            f"<version>{self.VERSION}</version></project>", encoding="utf-8")
        return self.root / "ci-maven"

    def _write_symbols(self, distribution):
        sym_root = self.root / "unstripped" / distribution
        for abi in ABI_ELF:
            dest = sym_root / abi
            dest.mkdir(parents=True)
            # Same Build ID + machine as the AAR lib, but distinct bytes.
            class64, machine = ABI_ELF[abi]
            data = build_elf(class64, machine, self.build_ids[abi]) + b"\x00debug"
            (dest / mod.BRIDGE_LIB).write_bytes(data)
        return sym_root

    def _jni_contract(self):
        p = self.root / "jni-contract.json"
        p.write_text(json.dumps({"apiContractVersion": 2, "jniContractVersion": 1,
                                 "methods": [{"name": "x", "descriptor": "()V",
                                              "static": True}]}), encoding="utf-8")
        return p

    def _lock(self):
        p = self.root / "lock.json"
        p.write_text(json.dumps({"vendorId": "vendor-x",
                                 "redistributionApproved": False}), encoding="utf-8")
        return p

    def _run(self, distribution):
        artifact_id = (mod.FULL_ARTIFACT if distribution == "full"
                       else mod.BRIDGE_ARTIFACT)
        aar = self._write_aar(distribution)
        maven = self._write_maven(artifact_id, aar)
        symbols = self._write_symbols(distribution)
        out = self.root / "dist"
        argv = ["--aar", str(aar), "--maven-dir", str(maven),
                "--version", self.VERSION, "--source-commit", self.COMMIT,
                "--distribution", distribution, "--output", str(out),
                "--unstripped-root", str(symbols),
                "--jni-contract", str(self._jni_contract())]
        if distribution == "full":
            argv += ["--lock", str(self._lock())]
        rc = mod.main(argv)
        self.assertEqual(0, rc)
        return artifact_id, out / f"avs3a-{distribution}-{self.VERSION}"

    def _assert_sha256sums(self, dist_root):
        for line in (dist_root / "SHA256SUMS").read_text().splitlines():
            digest, rel = line.split("  ", 1)
            self.assertEqual(digest, sha((dist_root / rel).read_bytes()),
                             f"SHA256SUMS mismatch for {rel}")

    def test_full_has_readme_and_per_arch_zips(self):
        artifact_id, dist_root = self._run("full")

        readme = dist_root / "README.md"
        self.assertTrue(readme.is_file())
        self.assertIn("发布包使用说明", readme.read_text(encoding="utf-8"))

        manifest = json.loads((dist_root / "sdk-manifest.json").read_text(encoding="utf-8"))
        self.assertEqual("README.md", manifest["usageGuide"])
        self.assertEqual(sorted(ABI_ELF),
                         sorted(p["abi"] for p in manifest["archPackages"]))
        files = {f["path"] for f in manifest["files"]}
        self.assertIn("README.md", files)

        for abi in ABI_ELF:
            zip_path = dist_root / "arch" / f"avs3a-full-{self.VERSION}-{abi}.zip"
            self.assertIn(zip_path.relative_to(dist_root).as_posix(), files)
            with zipfile.ZipFile(zip_path) as z:
                names = set(z.namelist())
                self.assertIn(f"{artifact_id}-{self.VERSION}-{abi}.aar", names)
                self.assertIn(f"jni/{abi}/{mod.BRIDGE_LIB}", names)
                self.assertIn(f"jni/{abi}/{mod.DECODER_LIB}", names)
                self.assertIn(f"symbols/{abi}/{mod.BRIDGE_LIB}.unstripped", names)
                self.assertIn("README.md", names)
                # The embedded single-ABI AAR must carry only this ABI's jni dir.
                inner = io.BytesIO(z.read(f"{artifact_id}-{self.VERSION}-{abi}.aar"))
                with zipfile.ZipFile(inner) as inner_aar:
                    jni_abis = {n.split("/")[1] for n in inner_aar.namelist()
                                if n.startswith("jni/") and n.count("/") >= 2}
                    self.assertEqual({abi}, jni_abis)
                    self.assertIn("assets/avs3a/model.bin", inner_aar.namelist())

        self._assert_sha256sums(dist_root)

    def test_bridge_arch_zip_excludes_decoder(self):
        artifact_id, dist_root = self._run("bridge")
        for abi in ABI_ELF:
            zip_path = dist_root / "arch" / f"avs3a-bridge-{self.VERSION}-{abi}.zip"
            with zipfile.ZipFile(zip_path) as z:
                names = z.namelist()
                self.assertNotIn(f"jni/{abi}/{mod.DECODER_LIB}", names)
                self.assertIn(f"jni/{abi}/{mod.BRIDGE_LIB}", names)
        self._assert_sha256sums(dist_root)


if __name__ == "__main__":
    unittest.main()
