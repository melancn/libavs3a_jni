#!/usr/bin/env python3
"""Verify both demo flavors and add installable APKs to an existing SDK distribution.

Signature verification is a separate apksigner step in release-sdk.yml. This
script checks payload/ABI boundaries, lock hashes and updates the manifest and
SHA256SUMS. It never claims device playback or grants redistribution permission.
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

ABIS = ("arm64-v8a", "armeabi-v7a")
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9]+)*")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify_apk(path, flavor, lock):
    with zipfile.ZipFile(path) as apk:
        names = apk.namelist()
        if len(names) != len(set(names)):
            raise ValueError(f"{flavor}: duplicate ZIP entries")
        if "AndroidManifest.xml" not in names or "classes.dex" not in names:
            raise ValueError(f"{flavor}: not an Android APK")
        for abi in ABIS:
            if f"lib/{abi}/libavs3a_jni.so" not in names:
                raise ValueError(f"{flavor}: missing JNI bridge for {abi}")
        native_abis = {n.split('/')[1] for n in names if n.startswith('lib/') and n.endswith('.so')}
        if native_abis != set(ABIS):
            raise ValueError(f"{flavor}: unexpected native ABIs: {sorted(native_abis)}")
        if flavor == "bridge":
            if any(Path(n).name in ("libavs3a_decoder.so", "model.bin") for n in names):
                raise ValueError("bridge: vendor/model leaked into bridge APK")
        else:
            for item in lock["files"]:
                source = item["source"]
                name = "assets/avs3a/model.bin" if source == "model.bin" else "lib/" + source
                if name not in names:
                    raise ValueError(f"full: missing {name}")
                data = apk.read(name)
                if len(data) != item["size"] or digest(data) != item["sha256"]:
                    raise ValueError(f"full: lock mismatch for {name}")


def package(bridge, full, version, lock_path, dist):
    if not VERSION.fullmatch(version):
        raise ValueError("Invalid SDK version")
    lock = json.loads(lock_path.read_text(encoding="utf-8-sig"))
    manifest_path = dist / "sdk-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if manifest.get("sdkVersion") != version or manifest.get("distribution") != "full":
        raise ValueError("SDK manifest version/distribution does not match demo package")
    # Validate both inputs before modifying the distribution.
    for flavor, path in (("bridge", bridge), ("full", full)):
        verify_apk(path, flavor, lock)
    demo_entries = []
    for flavor, path in (("bridge", bridge), ("full", full)):
        relative = f"demo/avs3a-media3-demo-{flavor}-{version}-debug.apk"
        target = dist / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        entry = {"path": relative, "size": target.stat().st_size,
                 "sha256": digest(target.read_bytes())}
        manifest["files"] = [f for f in manifest["files"] if f["path"] != relative]
        manifest["files"].append(entry)
        demo_entries.append({**entry, "distribution": flavor, "buildType": "debug",
                             "signing": "Android debug key (verified separately by apksigner)",
                             "deviceValidation": "NOT_RUN"})
    manifest["demoApks"] = demo_entries
    manifest["files"].sort(key=lambda f: f["path"])
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = [f"{entry['sha256']}  {entry['path']}" for entry in manifest["files"]]
    lines.append(f"{digest(manifest_path.read_bytes())}  sdk-manifest.json")
    (dist / "SHA256SUMS").write_text("\n".join(sorted(lines)) + "\n", encoding="utf-8")
    return demo_entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("bridge", "full", "lock", "dist"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        for entry in package(args.bridge, args.full, args.version, args.lock, args.dist):
            print("OK:", entry["path"], entry["sha256"])
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as e:
        print("ERROR:", e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
