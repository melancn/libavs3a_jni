#!/usr/bin/env python3
"""Verify locked vendor inputs against vendor/manifest.lock.json.

Two explicit modes:
  staging:  --lock L --input-root DIR --stage-root DIR
            verify every source file (size + SHA256 + path safety), then
            copy it to the whitelisted destination inside the stage root.
  verify:   --lock L --staged-root DIR
            [--require-abi-ready abi-contract.json] [--require-dialect-ready
            frame-dialect.json]
            check already-staged bytes only; never downloads or copies.

Exit 0 on success, 1 on any mismatch, traversal attempt, unexpected file or
failed contract-readiness gate. Standard library only.
"""

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
EXPECTED_ABIS = {"arm64-v8a", "armeabi-v7a"}
# Destination whitelist: only the decoder JNI libraries and the model asset.
DEST_JNI_RE = re.compile(r"^sdk/src/full/jniLibs/([A-Za-z0-9._-]+)/libavs3a_decoder\.so$")
DEST_ASSET_RE = re.compile(r"^sdk/src/full/assets/avs3a/model\.bin$")
VENDOR_ABI_NAMES = ("arm64-v8a", "armeabi-v7a")


class VendorError(Exception):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_lock(path: Path) -> dict:
    try:
        lock = json.loads(path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError:
        raise VendorError(f"lock file not found: {path.name}")
    except (OSError, json.JSONDecodeError) as exc:
        raise VendorError(f"lock file unreadable/invalid JSON ({path.name}): {exc}")
    if lock.get("schemaVersion") != 1:
        raise VendorError("lock schemaVersion must be 1")
    files = lock.get("files")
    if not isinstance(files, list) or not files:
        raise VendorError("lock files must be a non-empty list")
    seen_sources, seen_dests = set(), set()
    for i, entry in enumerate(files):
        src, dst = entry.get("source"), entry.get("destination")
        size, sha = entry.get("size"), entry.get("sha256")
        if not isinstance(src, str) or not src or not isinstance(dst, str) or not dst:
            raise VendorError(f"files[{i}]: source/destination must be non-empty strings")
        for part in (src, dst):
            if (part.startswith("/") or "\\" in part or re.match(r"^[A-Za-z]:", part)
                    or ".." in part.split("/")):
                raise VendorError(f"files[{i}]: unsafe path {part!r}")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise VendorError(f"files[{i}]: size must be a positive integer")
        if not isinstance(sha, str) or not HEX64_RE.match(sha):
            raise VendorError(f"files[{i}]: sha256 must be 64 hex characters")
        if "abi" in entry and entry["abi"] not in EXPECTED_ABIS:
            raise VendorError(f"files[{i}]: unexpected abi {entry['abi']!r}")
        if src in seen_sources or dst in seen_dests:
            raise VendorError(f"files[{i}]: duplicate source or destination")
        seen_sources.add(src)
        seen_dests.add(dst)
    return lock


def checked_descendant(root: Path, rel: str, label: str) -> Path:
    """Resolve rel under root, rejecting traversal, absolute paths and symlinks."""
    if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel) or "\\" in rel:
        raise VendorError(f"{label}: unsafe path {rel!r}")
    parts = rel.split("/")
    if any(part in ("", "..") for part in parts):
        raise VendorError(f"{label}: traversal detected in {rel!r}")
    candidate = root.joinpath(*parts)
    if not candidate.exists():
        raise VendorError(f"{label}: missing {rel!r}")
    current = root
    for part in parts:
        current = current / part
        if current.is_symlink():
            raise VendorError(f"{label}: symlink not allowed at {rel!r}")
    try:
        resolved = candidate.resolve()
        root_resolved = root.resolve()
    except OSError as exc:
        raise VendorError(f"{label}: cannot resolve {rel!r}: {exc.strerror or exc}")
    if os.path.commonpath([str(root_resolved), str(resolved)]) != str(root_resolved):
        raise VendorError(f"{label}: path escapes root: {rel!r}")
    return candidate


def read_capped(path: Path, expected_size: int, label: str) -> bytes:
    try:
        with path.open("rb") as fh:
            data = fh.read(expected_size + 1)
    except OSError as exc:
        raise VendorError(f"{label}: cannot read: {exc.strerror or exc}")
    if len(data) != expected_size:
        raise VendorError(f"{label}: size {len(data)} != locked size {expected_size}")
    return data


def atomic_copy(destination: Path, data: bytes) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    tmp = destination.with_name(destination.name + ".ci-tmp")
    with tmp.open("wb") as fh:
        fh.write(data)
    os.replace(tmp, destination)


def list_files(root: Path) -> set:
    found = set()
    if not root.exists():
        return found
    for dirpath, dirnames, filenames in os.walk(root):
        for name in filenames:
            full = Path(dirpath) / name
            rel = full.relative_to(root).as_posix()
            if full.is_symlink():
                raise VendorError(f"symlink not allowed: {rel}")
            found.add(rel)
        for d in list(dirnames):
            if (Path(dirpath) / d).is_symlink():
                raise VendorError(f"symlinked directory not allowed: {Path(dirpath, d).relative_to(root).as_posix()}")
    return found


def validate_destination(entry: dict) -> str:
    dst = entry["destination"]
    m = DEST_JNI_RE.match(dst)
    if m:
        abi_dir = m.group(1)
        if entry.get("abi") != abi_dir:
            raise VendorError(f"destination abi {abi_dir!r} != lock abi {entry.get('abi')!r} for {dst}")
        return "jni"
    if DEST_ASSET_RE.match(dst):
        if entry.get("abi") is not None:
            raise VendorError(f"model asset destination must not carry abi: {dst}")
        return "asset"
    raise VendorError(f"destination outside whitelist (only decoder .so / model.bin allowed): {dst}")


def run_staging(lock: dict, input_root: Path, stage_root: Path) -> int:
    allowed_sources = {e["source"] for e in lock["files"]}
    present = list_files(input_root)
    extra = sorted(present - allowed_sources)
    if extra:
        raise VendorError(f"unexpected files under input-root: {extra}")
    missing = sorted(allowed_sources - present)
    if missing:
        raise VendorError(f"locked files missing under input-root: {missing}")
    copied = 0
    for entry in lock["files"]:
        label = entry["source"]
        kind = validate_destination(entry)
        src = checked_descendant(input_root, entry["source"], label)
        data = read_capped(src, entry["size"], label)
        digest = sha256_bytes(data)
        if digest != entry["sha256"]:
            raise VendorError(f"{label}: SHA256 mismatch (computed {digest[:12]}... expected {entry['sha256'][:12]}...)")
        destination = checked_descendant_write_target(stage_root, entry["destination"])
        atomic_copy(destination, data)
        verify = read_capped(destination, entry["size"], f"staged {entry['destination']}")
        if sha256_bytes(verify) != entry["sha256"]:
            raise VendorError(f"staged copy of {entry['destination']} does not match lock after write")
        copied += 1
    print(f"OK: staged {copied} vendor file(s); all destinations on the decoder/model whitelist")
    return 0


def checked_descendant_write_target(root: Path, rel: str) -> Path:
    if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel) or "\\" in rel:
        raise VendorError(f"destination unsafe: {rel!r}")
    parts = rel.split("/")
    if any(part in ("", "..") for part in parts):
        raise VendorError(f"traversal detected in destination {rel!r}")
    current = root
    for part in parts:
        current = current / part
        if current.is_symlink():
            raise VendorError(f"symlink not allowed in destination path: {rel!r}")
    resolved = current.resolve() if current.exists() else Path(os.path.normpath(str(current)))
    root_r = root.resolve()
    if os.path.commonpath([str(root_r), str(resolved)]) != str(root_r):
        raise VendorError(f"destination escapes stage root: {rel!r}")
    return current


def run_verify(lock: dict, staged_root: Path) -> int:
    allowed_dests = {e["destination"] for e in lock["files"]}
    checked = 0
    for entry in lock["files"]:
        validate_destination(entry)
        dst_rel = entry["destination"]
        dst = checked_descendant(staged_root, dst_rel, f"staged {dst_rel}")
        data = read_capped(dst, entry["size"], f"staged {dst_rel}")
        digest = sha256_bytes(data)
        if digest != entry["sha256"]:
            raise VendorError(f"staged {dst_rel}: SHA256 mismatch (computed {digest[:12]}... expected {entry['sha256'][:12]}...)")
        checked += 1
    for sub in ("sdk/src/full/jniLibs", "sdk/src/full/assets"):
        base = staged_root / sub
        if not base.is_dir():
            continue
        present = {base.joinpath(*p.split("/")).relative_to(staged_root).as_posix()
                   for p in list_files(base)}
        extra = sorted(present - allowed_dests)
        if extra:
            raise VendorError(f"unexpected files under {sub}: {extra}")
    print(f"OK: verified {checked} staged vendor file(s) against lock")
    return 0


def require_abi_ready(path: Path) -> None:
    try:
        contract = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise VendorError(f"abi-contract unreadable/invalid JSON ({path.name}): {exc}")
    if contract.get("schemaVersion") != 1:
        raise VendorError("abi-contract schemaVersion must be 1")
    if contract.get("requiredFieldsResolved") is not True:
        raise VendorError("abi-contract requiredFieldsResolved is not true")
    required = contract.get("requiredFields") or []
    if not required:
        raise VendorError("abi-contract requiredFields empty")
    abis = contract.get("abis") or {}
    for abi in VENDOR_ABI_NAMES:
        entry = abis.get(abi)
        if not isinstance(entry, dict):
            raise VendorError(f"abi-contract missing entry for {abi}")
        if entry.get("ready") is not True:
            raise VendorError(f"abi {abi} not ready in abi-contract")
        if entry.get("callingConventionVerified") is not True:
            raise VendorError(f"abi {abi} calling convention not verified")
        fields = entry.get("fields") or {}
        unverified = [n for n in required
                      if n not in fields or fields[n].get("verified") is not True
                      or not (isinstance(fields[n].get("evidence"), list) and fields[n]["evidence"])]
        if unverified:
            raise VendorError(f"abi {abi}: required fields not verified or lacking evidence: {unverified}")
    print(f"OK: abi-contract declares both vendor ABIs ready with full evidence")


def require_dialect_ready(path: Path) -> None:
    try:
        dialect = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise VendorError(f"dialect-contract unreadable/invalid JSON ({path.name}): {exc}")
    if dialect.get("schemaVersion") != 1:
        raise VendorError("frame-dialect schemaVersion must be 1")
    if dialect.get("verified") is not True:
        raise VendorError("frame-dialect verified is not true")
    table = dialect.get("crcRules", {}).get("table")
    if not isinstance(table, list) or len(table) != 256:
        raise VendorError("frame-dialect crc table must have 256 entries")
    print("OK: frame-dialect verified")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--input-root", type=Path, help="staging mode: verified sources")
    parser.add_argument("--stage-root", type=Path, help="staging mode: destination project root")
    parser.add_argument("--staged-root", type=Path, help="verify mode: project root with staged files")
    parser.add_argument("--require-abi-ready", type=Path, metavar="ABI_CONTRACT")
    parser.add_argument("--require-dialect-ready", type=Path, metavar="DIALECT_CONTRACT")
    args = parser.parse_args(argv)

    staging = args.input_root is not None or args.stage_root is not None
    verifying = args.staged_root is not None
    if staging and verifying:
        print("ERROR: choose exactly one mode: staging (--input-root + --stage-root) or verify (--staged-root)",
              file=sys.stderr)
        return 2
    if staging and not (args.input_root and args.stage_root):
        print("ERROR: staging mode requires both --input-root and --stage-root", file=sys.stderr)
        return 2
    if not staging and not verifying:
        print("ERROR: no mode selected (see usage)", file=sys.stderr)
        return 2
    if verifying and (args.require_abi_ready is None or args.require_dialect_ready is None):
        print("ERROR: verify mode must pass --require-abi-ready and --require-dialect-ready",
              file=sys.stderr)
        return 2

    try:
        lock = load_lock(args.lock)
        if staging:
            rc = run_staging(lock, args.input_root.resolve(), args.stage_root.resolve())
        else:
            rc = run_verify(lock, args.staged_root.resolve())
            require_abi_ready(args.require_abi_ready)
            require_dialect_ready(args.require_dialect_ready)
        return rc
    except VendorError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
