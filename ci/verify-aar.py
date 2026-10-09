#!/usr/bin/env python3
"""Verify a built AAR (bridge or full distribution).

Checks ZIP structure, classes.jar + NativeBridge JNI signatures against
protocol/jni-contract.json (via javap when available), the JNI .so per expected
ABI with no extra ABIs, ELF machine/NEEDED/alignment via NDK llvm-readelf, and
distribution-specific content rules (bridge: no decoder/model/test artifacts;
full: decoder/model size+SHA256 exactly matching vendor/manifest.lock.json).
Exit 0 on success, 1 on failure. Standard library only.
"""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

EXPECTED_ABIS_DEFAULT = ["arm64-v8a", "armeabi-v7a"]
HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
# "assets" is tolerated at top level (full needs model.bin); the bridge-specific
# content check below still forbids any assets entry for bridge distributions.
# "proguard.txt" is emitted for the declared consumer-rules.pro; "META-INF" is
# AGP's standard AAR metadata directory. Both are normal AGP outputs.
ALLOWED_TOP_LEVEL = {"AndroidManifest.xml", "classes.jar", "R.txt", "jni", "assets",
                     "proguard.txt", "META-INF"}
BRIDGE_LIB = "libavs3a_jni.so"
DECODER_LIB = "libavs3a_decoder.so"
MODEL_ENTRY = "assets/avs3a/model.bin"
FORBIDDEN_CLASS_TOKENS = ("mediahub", "fakebackend", "ijkplayer", "ijksdl",
                          "ffmpeg", "media3", "exoplayer")
FORBIDDEN_NEEDED_TOKENS = ("av3a_decoder", "av3a_renderer", "ffmpeg", "ijk",
                           "renderer", "c++_shared", "media3", "exoplayer")
ABI_MACHINE = {"arm64-v8a": "aarch64", "armeabi-v7a": "arm"}


class AarError(Exception):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_json(path: Path, label: str) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AarError(f"{label} unreadable/invalid: {exc}")


def safe_member(name: str) -> bool:
    if name.startswith("/") or re.match(r"^[A-Za-z]:", name) or "\\" in name:
        return False
    return ".." not in name.split("/")


def inspect_zip(checker, zf: zipfile.ZipFile) -> set:
    names = set()
    for info in zf.infolist():
        name = info.filename
        if name.endswith("/"):
            continue
        if not safe_member(name):
            checker.error(f"unsafe zip member path: {name!r}")
            continue
        if name in names:
            checker.error(f"duplicate zip member: {name}")
            continue
        names.add(name)
    return names


def check_manifest_and_classes(checker, zf, names, tmp: Path):
    if "AndroidManifest.xml" not in names:
        checker.error("AAR root missing AndroidManifest.xml")
    if "classes.jar" not in names:
        checker.error("AAR root missing classes.jar")
        return None
    extra_roots = sorted({n.split("/")[0] for n in names} - ALLOWED_TOP_LEVEL)
    if extra_roots:
        checker.error(f"unexpected top-level AAR entries: {extra_roots}")
    classes_data = zf.read("classes.jar")
    jar_path = tmp / "classes.jar"
    jar_path.write_bytes(classes_data)
    try:
        jar = zipfile.ZipFile(jar_path)
    except zipfile.BadZipFile:
        raise AarError("classes.jar is not a valid ZIP")
    class_names = set()
    with jar:
        for info in jar.infolist():
            name = info.filename
            if name.endswith("/"):
                continue
            if not safe_member(name):
                checker.error(f"unsafe classes.jar member: {name!r}")
                continue
            if name.endswith(".class"):
                low = name.lower()
                for token in FORBIDDEN_CLASS_TOKENS:
                    if token in low:
                        checker.error(f"forbidden class in classes.jar: {name} (token {token!r})")
                base = name.rsplit("/", 1)[-1]
                if re.match(r".*(Test|Tests|IT|FakeBackend)\.class$", base):
                    checker.error(f"test/fake class must not ship: {name}")
                class_names.add(name)
        if "com/inlz/avs3a/NativeBridge.class" not in class_names:
            checker.error("classes.jar missing com/inlz/avs3a/NativeBridge.class")
        if not class_names:
            checker.error("classes.jar contains no classes")
        extracted_dir = tmp / "classes"
        jar.extractall(extracted_dir)
    return extracted_dir


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None


def verify_jni_signatures(checker, extracted_dir: Path, jni_contract: dict) -> None:
    methods = {m["name"]: (m["descriptor"], bool(m["static"])) for m in jni_contract["methods"]}
    javap = shutil.which("javap")
    if javap is None:
        checker.warn("javap not available; NativeBridge signature-vs-contract comparison skipped "
                     "(class presence only)")
        return
    proc = run([javap, "-p", "-s", "-classpath", str(extracted_dir), "com.inlz.avs3a.NativeBridge"])
    if proc is None or proc.returncode != 0:
        raise AarError("javap failed on classes.jar (corrupt class file?)")
    found = {}
    current = None
    for line in proc.stdout.splitlines():
        m = re.match(r"^\s+(static\s+)?native\s+\S+\s+(\w+)\(([^)]*)\);\s*$", line)
        if m:
            current = m.group(2)
            found[current] = [m.group(1) is not None, None]
            continue
        m = re.match(r"^\s+descriptor:\s+(\S+)\s*$", line)
        if m and current:
            found[current][1] = m.group(1)
            current = None
    for name, (descriptor, is_static) in methods.items():
        if name not in found:
            checker.error(f"NativeBridge missing declared native method {name}")
            continue
        got_static, got_descriptor = found[name]
        if got_descriptor != descriptor:
            checker.error(f"NativeBridge.{name} descriptor {got_descriptor!r} != contract {descriptor!r}")
        if got_static != is_static:
            checker.error(f"NativeBridge.{name} static mismatch (contract static={is_static})")
    extra = sorted(set(found) - set(methods))
    if extra:
        checker.error(f"undeclared native methods in NativeBridge: {extra}")
    if not checker.errors or all("NativeBridge" not in e for e in checker.errors):
        checker.ok(f"NativeBridge matches JNI contract ({len(methods)} methods)")


def verify_build_config(checker, extracted_dir: Path, expect_version, expect_commit) -> None:
    if not (expect_version or expect_commit):
        return
    javap = shutil.which("javap")
    if javap is None:
        checker.warn("javap not available; BuildConfig identity check skipped")
        return
    proc = run([javap, "-p", "-constants", "-classpath", str(extracted_dir), "com.inlz.avs3a.BuildConfig"])
    if proc is None or proc.returncode != 0:
        checker.error("BuildConfig.class not found in classes.jar")
        return
    if expect_version is not None:
        m = re.search(r'SDK_VERSION\s*=\s*"([^"]*)"', proc.stdout)
        if not m or m.group(1) != expect_version:
            checker.error(f"BuildConfig.SDK_VERSION mismatch (expected pinned release version)")
    if expect_commit is not None:
        m = re.search(r'SOURCE_COMMIT\s*=\s*"([^"]*)"', proc.stdout)
        if not m or m.group(1) != expect_commit:
            checker.error(f"BuildConfig.SOURCE_COMMIT mismatch (expected pinned commit)")
        m = re.search(r'API_CONTRACT_VERSION\s*=\s*(\d+)', proc.stdout)
        if not m or not m.group(1).isdigit():
            checker.error("BuildConfig.API_CONTRACT_VERSION missing")


def find_readelf(ndk: Path):
    patterns = [
        ndk / "toolchains" / "llvm" / "prebuilt" / "*" / "bin" / "llvm-readelf",
        ndk / "toolchains" / "llvm" / "prebuilt" / "*" / "bin" / "llvm-readelf.exe",
    ]
    for pattern in patterns:
        hits = sorted(ndk.glob(str(pattern.relative_to(ndk))))
        if hits:
            return hits[0]
    return None


def check_elf(checker, readelf: Path, abi: str, entry: str, data: bytes, tmp: Path) -> None:
    so_path = tmp / f"{abi}-lib.so"
    so_path.write_bytes(data)
    proc = run([str(readelf), "-h", str(so_path)])
    if proc is None or proc.returncode != 0:
        raise AarError(f"readelf failed on {entry}")
    machine_m = re.search(r"Machine:\s+(.+)", proc.stdout)
    machine = (machine_m.group(1).strip().lower() if machine_m else "")
    expected = ABI_MACHINE[abi]
    if expected == "aarch64":
        if "aarch64" not in machine:
            checker.error(f"{entry}: Machine {machine!r} is not AArch64 (ABI spoofing?)")
    else:
        if machine != "arm" and not machine.startswith("arm "):
            checker.error(f"{entry}: Machine {machine!r} is not 32-bit ARM")
    proc = run([str(readelf), "-d", str(so_path)])
    if proc and proc.returncode == 0:
        needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", proc.stdout)
        for lib in needed:
            low = lib.lower()
            for token in FORBIDDEN_NEEDED_TOKENS:
                if token in low:
                    checker.error(f"{entry}: forbidden NEEDED library {lib!r}")
    else:
        checker.error(f"readelf -d failed on {entry}")
    if abi == "arm64-v8a":
        proc = run([str(readelf), "-l", str(so_path)])
        if proc and proc.returncode == 0:
            aligns = []
            for line in proc.stdout.splitlines():
                m = re.match(r"^\s+LOAD\s+.*0x([0-9a-f]+)\s*$", line)
                if m:
                    aligns.append(int(m.group(1), 16))
            if not aligns:
                checker.error(f"{entry}: no LOAD segments parsed")
            elif min(aligns) < 0x4000:
                checker.error(f"{entry}: arm64 LOAD alignment {min(aligns):#x} < 0x4000 (16KB pages)")
        else:
            checker.error(f"readelf -l failed on {entry}")


def check_jni_layout(checker, zf, names, expected_abis, readelf, tmp):
    jni_dirs = {}
    for name in names:
        if not name.startswith("jni/"):
            continue
        parts = name.split("/")
        if len(parts) != 3 or not parts[1]:
            checker.error(f"unexpected path inside jni/: {name}")
            continue
        jni_dirs.setdefault(parts[1], set()).add(parts[2])
    found_abis = set(jni_dirs)
    extra = sorted(found_abis - set(expected_abis))
    missing = sorted(set(expected_abis) - found_abis)
    if extra:
        checker.error(f"unexpected ABIs in AAR: {extra}")
    if missing:
        checker.error(f"missing ABIs in AAR: {missing}")
        return
    for abi in expected_abis:
        files = jni_dirs[abi]
        if BRIDGE_LIB not in files:
            checker.error(f"jni/{abi}/{BRIDGE_LIB} missing")
            continue
        unexpected = sorted(files - {BRIDGE_LIB, DECODER_LIB})
        if unexpected:
            checker.error(f"unexpected files in jni/{abi}: {unexpected}")
        if readelf is not None:
            entry = f"jni/{abi}/{BRIDGE_LIB}"
            check_elf(checker, readelf, abi, entry, zf.read(entry), tmp)
    checker.ok(f"jni layout exactly matches expected ABIs: {sorted(expected_abis)}")


def check_distribution_content(checker, zf, names, distribution, lock, tmp):
    has_decoder = [n for n in names if n.endswith(DECODER_LIB)]
    has_model = MODEL_ENTRY in names
    if distribution == "bridge":
        if has_decoder:
            checker.error(f"bridge AAR must not bundle vendor decoder: {sorted(has_decoder)}")
        if has_model:
            checker.error("bridge AAR must not bundle model asset")
        assets = sorted(n for n in names if n.startswith("assets/"))
        if assets:
            checker.error(f"bridge AAR must not bundle assets: {assets}")
        if not checker.errors:
            checker.ok("bridge AAR free of decoder/model/assets")
        return
    # full distribution
    if lock is None:
        raise AarError("full distribution requires the vendor lock (--lock)")
    for entry in lock["files"]:
        abi = entry.get("abi")
        if abi:
            member = f"jni/{abi}/{DECODER_LIB}"
        else:
            member = entry["destination"].split("sdk/src/full/")[-1]
        if member not in names:
            checker.error(f"full AAR missing locked vendor member {member}")
            continue
        data = zf.read(member)
        if len(data) != entry["size"]:
            checker.error(f"{member}: size {len(data)} != lock size {entry['size']}")
            continue
        digest = sha256_bytes(data)
        if digest != entry["sha256"]:
            checker.error(f"{member}: SHA256 mismatch against lock")
            continue
    if not any("vendor member" in e or "SHA256" in e or "size" in e for e in checker.errors):
        checker.ok("full AAR vendor members match lock size+SHA256")


class Checker:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)
        print(f"WARN: {msg}", file=sys.stderr)

    def ok(self, msg):
        print(f"[ok] {msg}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--aar", required=True, type=Path)
    parser.add_argument("--distribution", required=True, choices=["bridge", "full"])
    parser.add_argument("--expected-abis", nargs="+", default=EXPECTED_ABIS_DEFAULT)
    parser.add_argument("--ndk", type=Path, default=None)
    parser.add_argument("--jni-contract", type=Path, default=None)
    parser.add_argument("--lock", type=Path, default=None)
    parser.add_argument("--expect-version", default=None)
    parser.add_argument("--expect-commit", default=None)
    parser.add_argument("--json-report", type=Path, default=None)
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    jni_path = args.jni_contract or (root / "protocol" / "jni-contract.json")
    checker = Checker()
    try:
        jni_contract = load_json(jni_path, "jni-contract")
        if not isinstance(jni_contract.get("methods"), list) or not jni_contract["methods"]:
            raise AarError("jni-contract has no methods")
        lock = None
        if args.lock is not None or args.distribution == "full":
            lock_path = args.lock or (root / "vendor" / "manifest.lock.json")
            lock = load_json(lock_path, "vendor lock")

        if not args.aar.is_file():
            raise AarError(f"AAR not found: {args.aar.name}")
        if not zipfile.is_zipfile(args.aar):
            raise AarError("AAR is not a valid ZIP archive")

        readelf = None
        if args.ndk is not None:
            if not args.ndk.is_dir():
                raise AarError(f"NDK path not found: {args.ndk.name}")
            readelf = find_readelf(args.ndk)
            if readelf is None:
                raise AarError("llvm-readelf not found under the given NDK path")
        else:
            checker.warn("--ndk not provided: ELF machine/NEEDED/alignment checks skipped")

        with tempfile.TemporaryDirectory(prefix="verify-aar-") as tmpname:
            tmp = Path(tmpname)
            with zipfile.ZipFile(args.aar) as zf:
                names = inspect_zip(checker, zf)
                extracted = check_manifest_and_classes(checker, zf, names, tmp)
                if extracted is not None:
                    verify_jni_signatures(checker, extracted, jni_contract)
                    verify_build_config(checker, extracted, args.expect_version, args.expect_commit)
                check_jni_layout(checker, zf, names, args.expected_abis, readelf, tmp)
                check_distribution_content(checker, zf, names, args.distribution, lock, tmp)
    except AarError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    report = {
        "aar": args.aar.name,
        "distribution": args.distribution,
        "expectedAbis": sorted(args.expected_abis),
        "warnings": checker.warnings,
        "errors": checker.errors,
        "result": "PASS" if not checker.errors else "FAIL",
    }
    if args.json_report:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                    encoding="utf-8")
    if checker.errors:
        for err in checker.errors:
            print(f"ERROR: {err}", file=sys.stderr)
        print(f"FAILED: {len(checker.errors)} problem(s)", file=sys.stderr)
        return 1
    print(f"OK: AAR verification passed for distribution={args.distribution}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
