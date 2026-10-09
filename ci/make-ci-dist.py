#!/usr/bin/env python3
"""Assemble a locked, coordinate-only CI distribution package.

bridge: only com/inlz/avs3a/avs3a-sdk-bridge/<version>/ is taken from the CI
Maven repo; full: only com/inlz/avs3a/avs3a-sdk/<version>/. The whole
build/ci-maven tree is never packaged. Output contains the AAR, a Maven zip,
unstripped symbol packages matched by ELF Build ID, sdk-manifest.json and
SHA256SUMS. deviceValidation is fixed to NOT_RUN. Exit 0 on success.
Standard library only.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import zipfile
from pathlib import Path

SDK_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-.][0-9A-Za-z]+)*$")
COMMIT_RE = re.compile(r"^([0-9a-f]{40}|uncommitted)$")
HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
GROUP_PATH = "com/inlz/avs3a"
BRIDGE_ARTIFACT = "avs3a-sdk-bridge"
FULL_ARTIFACT = "avs3a-sdk"
BRIDGE_LIB = "libavs3a_jni.so"
ALLOWED_SUFFIXES = (".aar", ".pom", ".module", "-sources.jar", "-javadoc.jar", ".asc")


class DistError(Exception):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_elf_note_build_id(data: bytes):
    """Return (machine_hex, buildIdHex|None) for an ELF binary, or None."""
    if len(data) < 52 or data[:4] != b"\x7fELF":
        return None
    ei_class = data[4]
    little = data[5] == 1
    endian = "<" if little else ">"
    if ei_class == 2:
        e_machine = struct.unpack_from(endian + "H", data, 18)[0]
        e_shoff = struct.unpack_from(endian + "Q", data, 40)[0]
        e_shentsize = struct.unpack_from(endian + "H", data, 58)[0]
        e_shnum = struct.unpack_from(endian + "H", data, 60)[0]
    elif ei_class == 1:
        e_machine = struct.unpack_from(endian + "H", data, 18)[0]
        e_shoff = struct.unpack_from(endian + "I", data, 32)[0]
        e_shentsize = struct.unpack_from(endian + "H", data, 46)[0]
        e_shnum = struct.unpack_from(endian + "H", data, 48)[0]
    else:
        return None
    if e_shoff == 0 or e_shentsize == 0 or e_shnum == 0:
        return (f"0x{e_machine:x}", None)
    build_id = None
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        if off + e_shentsize > len(data):
            return (f"0x{e_machine:x}", None)
        if ei_class == 2:
            f = struct.unpack_from(endian + "IIQQQQIIQQ", data, off)
        else:
            f = struct.unpack_from(endian + "IIIIIIIIII", data, off)
        if f[1] != 4:  # SHT_NOTE
            continue
        p, end = f[4], f[4] + f[5]
        while p + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from(endian + "III", data, p)
            p += 12
            name = data[p:p + namesz]
            p += (namesz + 3) & ~3
            desc = data[p:p + descsz]
            p += (descsz + 3) & ~3
            if p > end:
                break
            if name.startswith(b"GNU") and ntype == 3 and descsz > 0:
                build_id = desc.hex()
    return (f"0x{e_machine:x}", build_id)


def expected_machine(abi: str) -> str:
    return "0xb7" if abi == "arm64-v8a" else "0x28"  # EM_AARCH64 / EM_ARM


def collect_aar_jni_libs(aar_path: Path):
    libs = {}
    with zipfile.ZipFile(aar_path) as zf:
        for name in zf.namelist():
            if not name.startswith("jni/") or not name.endswith(f"/{BRIDGE_LIB}"):
                continue
            abi = name.split("/")[1]
            data = zf.read(name)
            parsed = parse_elf_note_build_id(data)
            if parsed is None or parsed[1] is None:
                raise DistError(f"cannot read Build ID from {name} in AAR")
            machine, build_id = parsed
            if machine != expected_machine(abi):
                raise DistError(f"{name}: ELF machine {machine} does not match ABI {abi}")
            libs[abi] = {
                "abi": abi,
                "entry": name,
                "size": len(data),
                "sha256": sha256_bytes(data),
                "buildId": build_id,
            }
    if not libs:
        raise DistError("AAR contains no jni/<abi>/libavs3a_jni.so entries")
    return libs


def match_unstripped(unstripped_root: Path, aar_libs: dict) -> dict:
    if not unstripped_root.is_dir():
        raise DistError(f"unstripped symbol root not found: {unstripped_root.as_posix()}")
    candidates = sorted(p for p in unstripped_root.rglob(BRIDGE_LIB) if p.is_file())
    if not candidates:
        raise DistError("no unstripped libavs3a_jni.so files found under the symbol root")
    by_abi = {}
    for abi, info in aar_libs.items():
        matches = []
        for path in candidates:
            parsed = parse_elf_note_build_id(path.read_bytes())
            if parsed is None:
                continue
            machine, build_id = parsed
            if build_id == info["buildId"] and machine == expected_machine(abi):
                matches.append(path)
        if not matches:
            raise DistError(f"no unstripped symbol with Build ID matching AAR jni lib for {abi}")
        if len(matches) > 1:
            raise DistError(f"ambiguous unstripped symbols for {abi}: {len(matches)} Build ID matches")
        by_abi[abi] = matches[0]
    return by_abi


def validate_maven_coordinate(maven_dir: Path, artifact_id: str, version: str, aar_sha: str):
    if not maven_dir.is_dir():
        raise DistError(f"maven dir not found: {maven_dir.as_posix()}")
    version_dir = maven_dir / GROUP_PATH / artifact_id / version
    if not version_dir.is_dir():
        raise DistError(f"expected coordinate directory missing: {GROUP_PATH}/{artifact_id}/{version}")
    try:
        version_dir.resolve().relative_to(maven_dir.resolve())
    except ValueError:
        raise DistError("coordinate directory escapes the maven dir")
    files = sorted(p for p in version_dir.iterdir() if p.is_file())
    if not files:
        raise DistError(f"coordinate directory is empty: {version}")
    prefix = f"{artifact_id}-{version}"
    aars = []
    for p in files:
        if not p.name.startswith(prefix) or not any(p.name.endswith(s) for s in ALLOWED_SUFFIXES):
            raise DistError(
                f"unexpected file in coordinate dir (stale/foreign artifact): {p.name}; "
                "only the exact version directory of the requested coordinate is packaged")
        if p.name.endswith(".aar"):
            aars.append(p)
    if len(aars) != 1:
        raise DistError(f"expected exactly one AAR in coordinate dir, found {len(aars)}")
    if sha256_file(aars[0]) != aar_sha:
        raise DistError("AAR in maven coordinate dir does not match the requested --aar bytes")
    pom = version_dir / f"{prefix}.pom"
    if not pom.is_file():
        raise DistError(f"missing POM for coordinate: {prefix}.pom")
    pom_text = pom.read_text(encoding="utf-8-sig", errors="replace")

    def pom_field(tag):
        m = re.search(rf"<{tag}[^>]*>([^<]+)</{tag}>", pom_text)
        return m.group(1).strip() if m else None

    if pom_field("groupId") != "com.inlz.avs3a":
        raise DistError("POM groupId mismatch")
    if pom_field("artifactId") != artifact_id:
        raise DistError("POM artifactId mismatch")
    if pom_field("version") != version:
        raise DistError("POM version mismatch")
    return version_dir, files


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(content, encoding="utf-8", newline="\n")
    os.replace(tmp, path)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--aar", required=True, type=Path)
    parser.add_argument("--maven-dir", type=Path, default=Path("build/ci-maven"))
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--distribution", required=True, choices=["bridge", "full"])
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--unstripped-root", type=Path, default=None,
                        help="default: build/unstripped/<distribution>")
    parser.add_argument("--jni-contract", type=Path, default=None)
    parser.add_argument("--lock", type=Path, default=None)
    args = parser.parse_args(argv)

    if not SDK_VERSION_RE.match(args.version):
        print(f"ERROR: --version {args.version!r} is not a valid SDK version", file=sys.stderr)
        return 2
    if not COMMIT_RE.match(args.source_commit):
        print("ERROR: --source-commit must be 40 hex chars or 'uncommitted'", file=sys.stderr)
        return 2
    if not args.aar.is_file():
        print(f"ERROR: AAR not found: {args.aar.name}", file=sys.stderr)
        return 1

    artifact_id = BRIDGE_ARTIFACT if args.distribution == "bridge" else FULL_ARTIFACT
    root = Path(__file__).resolve().parents[1]
    jni_path = args.jni_contract or (root / "protocol" / "jni-contract.json")
    aar_sha = sha256_file(args.aar)
    dist_root = args.output / f"avs3a-{args.distribution}-{args.version}"

    try:
        version_dir, coordinate_files = validate_maven_coordinate(
            args.maven_dir, artifact_id, args.version, aar_sha)
        aar_libs = collect_aar_jni_libs(args.aar)
        unstripped_root = args.unstripped_root or (Path("build") / "unstripped" / args.distribution)
        symbols = match_unstripped(unstripped_root, aar_libs)

        if dist_root.exists():
            shutil.rmtree(dist_root)
        dist_root.mkdir(parents=True)

        # 1. AAR copy
        aar_out = dist_root / "aar" / f"{artifact_id}-{args.version}.aar"
        aar_out.parent.mkdir(parents=True)
        shutil.copy2(args.aar, aar_out)

        # 2. Maven zip (coordinate-only files; the rest of ci-maven is never packaged)
        maven_rel = Path(GROUP_PATH) / artifact_id / args.version
        staged_maven = dist_root / "maven" / maven_rel
        staged_maven.mkdir(parents=True)
        for src in coordinate_files:
            shutil.copy2(src, staged_maven / src.name)
        maven_zip = dist_root / "maven" / f"{artifact_id}-{args.version}-maven.zip"
        with zipfile.ZipFile(maven_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for src in coordinate_files:
                zf.write(staged_maven / src.name, (maven_rel / src.name).as_posix())

        # 3. Unstripped symbol packages (Build-ID matched, one per ABI)
        symbols_out = {}
        for abi, src in symbols.items():
            dest = dist_root / "symbols" / abi / f"{BRIDGE_LIB}.unstripped"
            dest.parent.mkdir(parents=True)
            shutil.copy2(src, dest)
            data = src.read_bytes()
            parsed = parse_elf_note_build_id(data)
            symbols_out[abi] = {
                "abi": abi,
                "path": dest.relative_to(dist_root).as_posix(),
                "size": len(data),
                "sha256": sha256_bytes(data),
                "buildId": parsed[1] if parsed else None,
            }

        # 4. Contract identity inputs
        jni = json.loads(jni_path.read_text(encoding="utf-8-sig"))
        vendor = {"locked": args.distribution == "full"}
        if args.distribution == "full":
            lock_path = args.lock or (root / "vendor" / "manifest.lock.json")
            lock = json.loads(lock_path.read_text(encoding="utf-8-sig"))
            vendor["manifestLockSha256"] = sha256_file(lock_path)
            vendor["vendorId"] = lock.get("vendorId")
            vendor["redistributionApproved"] = lock.get("redistributionApproved")

        manifest = {
            "aar": {
                "file": aar_out.relative_to(dist_root).as_posix(),
                "sha256": aar_sha,
                "size": aar_out.stat().st_size,
            },
            "apiContractVersion": jni.get("apiContractVersion"),
            "artifactId": artifact_id,
            "distribution": args.distribution,
            "deviceValidation": "NOT_RUN",
            "deviceValidationNote": "host compilation and host tests never imply ARM device PASS",
            "files": [],
            "groupId": "com.inlz.avs3a",
            "jniContractVersion": jni.get("jniContractVersion"),
            "jniLibs": [aar_libs[abi] for abi in sorted(aar_libs)],
            "runtimeVendorValidation": "NOT_RUN",
            "schemaVersion": 1,
            "sdkVersion": args.version,
            "sourceCommit": args.source_commit,
            "symbols": [symbols_out[abi] for abi in sorted(symbols_out)],
            "vendor": vendor,
        }
        for out_file in sorted(p for p in dist_root.rglob("*") if p.is_file()):
            rel = out_file.relative_to(dist_root).as_posix()
            if rel in ("sdk-manifest.json", "SHA256SUMS"):
                continue
            manifest["files"].append({
                "path": rel,
                "size": out_file.stat().st_size,
                "sha256": sha256_file(out_file),
            })
        atomic_write(dist_root / "sdk-manifest.json",
                     json.dumps(manifest, indent=2, sort_keys=True) + "\n")

        # 5. SHA256SUMS over every emitted file (deterministic order, relative paths)
        lines = [f"{e['sha256']}  {e['path']}" for e in manifest["files"]]
        lines.append(f"{sha256_file(dist_root / 'sdk-manifest.json')}  sdk-manifest.json")
        atomic_write(dist_root / "SHA256SUMS", "\n".join(sorted(lines)) + "\n")
    except (DistError, OSError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"OK: wrote coordinate-only distribution at {dist_root.as_posix()} "
          f"({len(manifest['files']) + 2} files) with deviceValidation=NOT_RUN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
