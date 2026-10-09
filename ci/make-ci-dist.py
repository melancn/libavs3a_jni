#!/usr/bin/env python3
"""Produce a locked distribution directory from a built AAR and Maven repo.

Outputs: AAR copy, Maven coordinate-only zip, unstripped symbol package,
sdk-manifest.json, SHA256SUMS. deviceValidation is always NOT_RUN here.
Exit 0 on success, non-zero on error. Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile
from pathlib import Path

VERSION_RE = __import__("re").compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9]+)*$")
SHA256_RE = __import__("re").compile(r"^[0-9a-f]{64}$")
COMMIT_RE = __import__("re").compile(r"^[0-9a-f]{40}$")


def fail(msg):
    print(f"ERROR: {msg}", file=sys.stderr)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def checked_descendant(root, relative):
    p = Path(relative)
    if p.is_absolute() or ".." in p.parts:
        raise ValueError(f"path escapes root: {relative}")
    full = (Path(root) / p).resolve()
    try:
        full.relative_to(Path(root).resolve())
    except ValueError:
        raise ValueError(f"path escapes root: {relative}")
    return full


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--aar", required=True, type=Path)
    parser.add_argument("--maven-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--distribution", required=True, choices=["bridge", "full"])
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)

    try:
        if not VERSION_RE.match(args.version):
            raise ValueError(f"invalid version: {args.version}")
        if args.source_commit != "uncommitted" and not COMMIT_RE.match(args.source_commit):
            raise ValueError(f"invalid source-commit: {args.source_commit}")
        if not args.aar.is_file():
            raise ValueError(f"AAR not found: {args.aar}")

        artifact_id = "avs3a-sdk-bridge" if args.distribution == "bridge" else "avs3a-sdk"
        group_path = Path("com") / "inlz" / "avs3a" / artifact_id / args.version
        version_dir = args.maven_dir / group_path

        if not version_dir.is_dir():
            raise ValueError(f"Maven version dir not found: {version_dir}")

        aar_hash = sha256_file(args.aar)

        maven_aar = None
        for f in version_dir.iterdir():
            if f.suffix == ".aar":
                maven_aar = f
                break
        if not maven_aar:
            raise ValueError(f"No AAR in Maven dir: {version_dir}")
        if sha256_file(maven_aar) != aar_hash:
            raise ValueError("Maven AAR hash != input AAR hash")

        args.output.mkdir(parents=True, exist_ok=True)

        out_aar = args.output / args.aar.name
        shutil.copy2(args.aar, out_aar)

        maven_zip = args.output / f"{artifact_id}-{args.version}-maven.zip"
        with zipfile.ZipFile(maven_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in sorted(version_dir.rglob("*")):
                if f.is_file():
                    arcname = str(f.relative_to(args.maven_dir))
                    zf.write(f, arcname)

        symbol_dir = args.output / "symbols"
        unstripped_base = Path(args.maven_dir).parent / "unstripped" / args.distribution
        if unstripped_base.is_dir():
            symbol_dir.mkdir(exist_ok=True)
            for abi in ("arm64-v8a", "armeabi-v7a"):
                for build_type in ("Release", "Debug"):
                    candidate = unstripped_base / build_type / abi / "libavs3a_jni.so"
                    if candidate.is_file():
                        dst = symbol_dir / f"libavs3a_jni-{abi}-{build_type}.so"
                        shutil.copy2(candidate, dst)

        manifest = {
            "schemaVersion": 1,
            "distribution": args.distribution,
            "version": args.version,
            "sourceCommit": args.source_commit,
            "apiContractVersion": 1,
            "jniContractVersion": 1,
            "aarSha256": aar_hash,
            "deviceValidation": "NOT_RUN",
            "files": [],
        }

        for f in sorted(args.output.rglob("*")):
            if f.is_file():
                rel = f.relative_to(args.output)
                manifest["files"].append({
                    "path": str(rel),
                    "sha256": sha256_file(f),
                    "bytes": f.stat().st_size,
                })

        manifest_path = args.output / "sdk-manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        sums_path = args.output / "SHA256SUMS"
        lines = []
        for entry in manifest["files"]:
            lines.append(f"{entry['sha256']}  {entry['path']}")
        sums_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

        print(json.dumps({
            "status": "OK",
            "distribution": args.distribution,
            "version": args.version,
            "files": len(manifest["files"]),
            "deviceValidation": "NOT_RUN",
        }, indent=2))
        return 0

    except Exception as exc:
        fail(str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
