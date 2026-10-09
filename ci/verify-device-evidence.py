#!/usr/bin/env python3
"""Verify device evidence against the published full AAR and fixture manifest.

Checks: AAR SHA matches, SDK version/sourceCommit/contract versions match,
process ABI is real ARM (pointerBytes 8 or 4), no duplicate ABI reports,
test results present. Exit non-zero on any failure. Standard library only.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def fail(msg):
    print(f"ERROR: {msg}", file=sys.stderr)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--aar", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--device-report", required=True, type=Path)
    parser.add_argument("--fixture-manifest", required=True, type=Path)
    args = parser.parse_args(argv)

    try:
        if not args.aar.is_file():
            raise ValueError(f"AAR not found: {args.aar}")
        if not args.manifest.is_file():
            raise ValueError(f"manifest not found: {args.manifest}")
        if not args.fixture_manifest.is_file():
            raise ValueError(f"fixture manifest not found: {args.fixture_manifest}")

        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        fixture_manifest = json.loads(args.fixture_manifest.read_text(encoding="utf-8"))

        if manifest.get("distribution") != "full":
            raise ValueError("device evidence can only verify full distribution")
        if manifest.get("deviceValidation") == "PASS":
            raise ValueError("manifest deviceValidation must not be pre-set to PASS")

        aar_hash = sha256_file(args.aar)
        if manifest.get("aarSha256") != aar_hash:
            raise ValueError("AAR SHA mismatch between file and manifest")

        expected_version = manifest.get("version")
        expected_commit = manifest.get("sourceCommit")
        expected_api = manifest.get("apiContractVersion", 1)
        expected_jni = manifest.get("jniContractVersion", 1)

        reports = {}
        if args.device_report.is_dir():
            for abi_dir in sorted(args.device_report.iterdir()):
                if abi_dir.is_dir() and abi_dir.name in ("arm64-v8a", "armeabi-v7a"):
                    report_file = abi_dir / "report.json"
                    if report_file.is_file():
                        reports[abi_dir.name] = json.loads(
                            report_file.read_text(encoding="utf-8"))
        elif args.device_report.is_file():
            single = json.loads(args.device_report.read_text(encoding="utf-8"))
            abi = single.get("process", {}).get("abi")
            if abi in ("arm64-v8a", "armeabi-v7a"):
                reports[abi] = single

        if len(reports) != 2:
            raise ValueError(
                f"expected exactly 2 ABI reports (arm64-v8a + armeabi-v7a), got {len(reports)}")

        seen_abis = set()
        for abi_name, report in reports.items():
            if abi_name in seen_abis:
                raise ValueError(f"duplicate ABI report: {abi_name}")
            seen_abis.add(abi_name)

            status = report.get("status")
            if status != "PASS" and status != "NOT_RUN":
                raise ValueError(f"{abi_name}: unexpected status {status}")

            if report.get("aarSha256") and report["aarSha256"] != aar_hash:
                raise ValueError(f"{abi_name}: AAR SHA mismatch in report")

            if report.get("sdkVersion") and report["sdkVersion"] != expected_version:
                raise ValueError(f"{abi_name}: version mismatch")

            if report.get("sourceCommit") and report["sourceCommit"] != expected_commit:
                raise ValueError(f"{abi_name}: sourceCommit mismatch")

            if report.get("apiContractVersion") != expected_api:
                raise ValueError(f"{abi_name}: API contract mismatch")

            if report.get("jniContractVersion") != expected_jni:
                raise ValueError(f"{abi_name}: JNI contract mismatch")

            ptr = report.get("process", {}).get("pointerBytes")
            expected_ptr = 8 if abi_name == "arm64-v8a" else 4
            if ptr != expected_ptr:
                raise ValueError(
                    f"{abi_name}: pointerBytes {ptr} != expected {expected_ptr}")

            fixtures = report.get("fixtures", [])
            if not fixtures:
                raise ValueError(f"{abi_name}: no fixture results in report")

        if fixture_manifest.get("status") == "NOT_READY":
            raise ValueError("fixture manifest status is NOT_READY")

        print(json.dumps({
            "status": "VERIFIED" if all(
                r.get("status") == "PASS" for r in reports.values()) else "PARTIAL",
            "abis": sorted(reports.keys()),
            "version": expected_version,
        }, indent=2))
        return 0

    except Exception as exc:
        fail(str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
