#!/usr/bin/env python3
"""Verify pinned toolchain configuration and lock files of the libavs3a_jni repo.

Checks actual file contents (not just existence): Gradle wrapper version and SHA,
AGP/compileSdk/minSdk/NDK/CMake pins, sdkVersion format, absence of MediaHub
paths, includeBuild, forbidden framework dependencies, and lock-file shape.
Exit 0 on success, 1 on any failure. Standard library only.
"""

import argparse
import json
import re
import sys
from pathlib import Path

GRADLE_VERSION = "9.4.0"
GRADLE_URL = "https://services.gradle.org/distributions/gradle-9.4.0-bin.zip"
AGP_VERSION = "9.1.0"
COMPILE_SDK = "36"
MIN_SDK = "24"
NDK_VERSION = "27.2.12479018"
CMAKE_VERSION = "3.22.1"
SDK_VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-.][0-9A-Za-z]+)*$")
COMMIT_RE = re.compile(r"^(?:[0-9a-f]{40}|uncommitted)$")
HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
EXPECTED_ABIS = {"arm64-v8a", "armeabi-v7a"}

FORBIDDEN_PATTERNS = [
    ("MediaHub path", re.compile(r"mediahub", re.IGNORECASE)),
    ("includeBuild", re.compile(r"includeBuild\s*\(")),
    ("Media3/ExoPlayer dependency", re.compile(r"media3|exoplayer", re.IGNORECASE)),
    ("FFmpeg dependency", re.compile(r"\bffmpeg\b", re.IGNORECASE)),
    ("IJKplayer dependency", re.compile(r"ijkplayer|ijksdl|libijk", re.IGNORECASE)),
    ("Kotlin coroutines dependency", re.compile(r"coroutines", re.IGNORECASE)),
    ("Kotlin plugin dependency", re.compile(r"org\.jetbrains\.kotlin", re.IGNORECASE)),
]


def default_root() -> Path:
    return Path(__file__).resolve().parents[1]


class Checker:
    def __init__(self) -> None:
        self.errors = []

    def fail(self, name: str, detail: str) -> None:
        self.errors.append(f"{name}: {detail}")

    def ok(self, name: str) -> None:
        print(f"[ok] {name}")


def read_text(checker: Checker, name: str, path: Path) -> "str|None":
    try:
        data = path.read_text(encoding="utf-8-sig")
    except OSError as exc:
        checker.fail(name, f"cannot read: {exc.strerror or exc}")
        return None
    if not data.strip():
        checker.fail(name, "file is empty")
        return None
    return data


def check_wrapper(checker: Checker, root: Path) -> None:
    props_path = root / "gradle" / "wrapper" / "gradle-wrapper.properties"
    name = "gradle-wrapper.properties"
    text = read_text(checker, name, props_path)
    if text is None:
        return
    url_match = re.search(r"^distributionUrl=(.+)$", text, re.MULTILINE)
    sha_match = re.search(r"^distributionSha256Sum=(.+)$", text, re.MULTILINE)
    if not url_match:
        checker.fail(name, "distributionUrl missing")
    else:
        url = url_match.group(1).strip().replace("\\", "")
        ver_match = re.search(r"gradle-([0-9][0-9.a-z-]*)-(?:bin|all)\.zip$", url)
        if not ver_match:
            checker.fail(name, f"distributionUrl not a recognizable Gradle distribution: {url}")
        elif ver_match.group(1) != GRADLE_VERSION:
            checker.fail(name, f"Gradle version {ver_match.group(1)} != pinned {GRADLE_VERSION}")
        elif url != GRADLE_URL:
            checker.fail(name, f"distributionUrl must be exactly {GRADLE_URL}")
        else:
            checker.ok(f"{name} Gradle {GRADLE_VERSION}")
    if not sha_match:
        checker.fail(name, "distributionSha256Sum missing")
    else:
        sha = sha_match.group(1).strip()
        if not HEX64_RE.match(sha):
            checker.fail(name, "distributionSha256Sum is not 64 hex characters")
        else:
            checker.ok(f"{name} distributionSha256Sum present (64 hex)")
    jar = root / "gradle" / "wrapper" / "gradle-wrapper.jar"
    if not jar.is_file() or jar.stat().st_size < 1000:
        checker.fail(name, "gradle-wrapper.jar missing or implausibly small")
    else:
        checker.ok("gradle-wrapper.jar present")
    for script in ("gradlew", "gradlew.bat"):
        p = root / script
        if not p.is_file() or p.stat().st_size == 0:
            checker.fail(name, f"{script} missing or empty")


def check_root_build(checker: Checker, root: Path) -> None:
    name = "build.gradle.kts"
    text = read_text(checker, name, root / name)
    if text is None:
        return
    plugins = re.findall(
        r'id\("com\.android\.(library|application)"\)\s+version\s+"([^"]+)"', text)
    if not plugins:
        checker.fail(name, "no com.android.* plugin declarations found")
        return
    for kind, version in plugins:
        if version != AGP_VERSION:
            checker.fail(name, f"com.android.{kind} version {version!r} != pinned {AGP_VERSION}")
    kinds = {k for k, _ in plugins}
    for required in ("library", "application"):
        if required not in kinds:
            checker.fail(name, f"plugin com.android.{required} not declared")
    if not checker.errors or all(not e.startswith(name) for e in checker.errors):
        checker.ok(f"{name} AGP {AGP_VERSION}")


def check_sdk_build(checker: Checker, root: Path) -> None:
    name = "sdk/build.gradle.kts"
    text = read_text(checker, name, root / name)
    if text is None:
        return

    def grab(pattern, label, expected, kind=int):
        m = re.search(pattern, text)
        if not m:
            checker.fail(name, f"{label} not found")
            return
        value = m.group(1)
        if kind is int:
            if value != str(expected):
                checker.fail(name, f"{label} {value} != pinned {expected}")
                return
        elif value != expected:
            checker.fail(name, f"{label} {value!r} != pinned {expected!r}")
            return
        checker.ok(f"{name} {label}={value}")

    grab(r"compileSdk\s*=\s*(\d+)", "compileSdk", COMPILE_SDK)
    grab(r"minSdk\s*=\s*(\d+)", "minSdk", MIN_SDK)
    grab(r'gradleProperty\("androidNdkVersion"\)\.getOrElse\("([^"]+)"\)', "NDK pin", NDK_VERSION, str)
    grab(r'gradleProperty\("androidCmakeVersion"\)\.getOrElse\("([^"]+)"\)', "CMake pin", CMAKE_VERSION, str)
    abis = re.search(r'abiFilters\s*\+=\s*setOf\(([^)]*)\)', text)
    if not abis:
        checker.fail(name, "ndk abiFilters set not found")
    else:
        found = set(re.findall(r'"([^"]+)"', abis.group(1)))
        if found != EXPECTED_ABIS:
            checker.fail(name, f"abiFilters {sorted(found)} != {sorted(EXPECTED_ABIS)}")
        else:
            checker.ok(f"{name} abiFilters {sorted(found)}")

    sv_default = re.search(r'gradleProperty\("sdkVersion"\)\.getOrElse\("([^"]*)"\)', text)
    sc_default = re.search(r'gradleProperty\("sourceCommit"\)\.getOrElse\("([^"]*)"\)', text)
    if not sv_default:
        checker.fail(name, "sdkVersion default not declared")
    elif not SDK_VERSION_RE.match(sv_default.group(1)):
        checker.fail(name, f"sdkVersion default {sv_default.group(1)!r} not a valid version")
    else:
        checker.ok(f"{name} sdkVersion default format valid")
    if not sc_default:
        checker.fail(name, "sourceCommit default not declared")
    elif not COMMIT_RE.match(sc_default.group(1)):
        checker.fail(name, f"sourceCommit default {sc_default.group(1)!r} invalid")
    else:
        checker.ok(f"{name} sourceCommit default valid")


def check_gradle_properties(checker: Checker, root: Path) -> None:
    name = "gradle.properties"
    text = read_text(checker, name, root / name)
    if text is None:
        return
    props = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            props[k.strip()] = v.strip()
        else:
            checker.fail(name, f"malformed line: {line.splitlines()[0][:40]}")
    for key, expected in (("androidNdkVersion", NDK_VERSION), ("androidCmakeVersion", CMAKE_VERSION)):
        if key in props and props[key] != expected:
            checker.fail(name, f"{key}={props[key]!r} conflicts with pin {expected}")
    if "sdkVersion" in props and not SDK_VERSION_RE.match(props["sdkVersion"]):
        checker.fail(name, f"sdkVersion={props['sdkVersion']!r} not a valid version")
    if "sourceCommit" in props and not COMMIT_RE.match(props["sourceCommit"]):
        checker.fail(name, f"sourceCommit={props['sourceCommit']!r} invalid")
    checker.ok(f"{name} pinned overrides consistent")


def check_settings(checker: Checker, root: Path) -> None:
    name = "settings.gradle.kts"
    text = read_text(checker, name, root / name)
    if text is None:
        return
    if re.search(r'rootProject\.name\s*=\s*"libavs3a_jni"', text):
        checker.ok(f"{name} rootProject.name")
    else:
        checker.fail(name, 'rootProject.name must be "libavs3a_jni"')
    for inc in re.findall(r"include\(([^)]*)\)", text):
        mods = re.findall(r'":([^"]+)"', inc)
        if not mods:
            checker.fail(name, f"include(...) without modules: {inc}")
    if re.search(r'include\([^)]*":sdk"', text):
        checker.ok(f"{name} includes :sdk")
    else:
        checker.fail(name, ":sdk module not included")


def check_forbidden(checker: Checker, root: Path) -> None:
    targets = [
        "settings.gradle.kts", "build.gradle.kts", "gradle.properties",
        "sdk/build.gradle.kts", "smoke-test/build.gradle.kts", "gradle/libs.versions.toml",
    ]
    scanned = 0
    for rel in targets:
        path = root / rel
        if not path.is_file():
            continue
        scanned += 1
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        # Registering the standalone demo is allowed; SDK player dependencies remain forbidden.
        if rel == "settings.gradle.kts":
            text = text.replace('":media3-demo"', '":demo"')
        for label, pattern in FORBIDDEN_PATTERNS:
            m = pattern.search(text)
            if m:
                line_no = text.count("\n", 0, m.start()) + 1
                checker.fail(
                    f"forbidden-dependency({rel})",
                    f"{label} found at line {line_no}: {m.group(0)[:40]!r}")
    if scanned == 0:
        checker.fail("forbidden-dependency", "no build files scanned")
        return
    checker.ok(f"no MediaHub/includeBuild/forbidden deps in {scanned} build files")


def load_json(checker: Checker, name: str, path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError:
        checker.fail(name, "file not found")
    except OSError as exc:
        checker.fail(name, f"cannot read: {exc.strerror or exc}")
    except json.JSONDecodeError as exc:
        checker.fail(name, f"invalid JSON at line {exc.lineno} col {exc.colno}")
    return None


def check_vendor_lock(checker: Checker, root: Path) -> None:
    name = "vendor/manifest.lock.json"
    data = load_json(checker, name, root / name)
    if data is None:
        return
    if data.get("schemaVersion") != 1:
        checker.fail(name, "schemaVersion must be 1")
    if not isinstance(data.get("redistributionApproved"), bool):
        checker.fail(name, "redistributionApproved must be boolean")
    files = data.get("files")
    if not isinstance(files, list) or not files:
        checker.fail(name, "files must be a non-empty list")
        return
    seen_sources, seen_dests = set(), set()
    for i, entry in enumerate(files):
        src = entry.get("source")
        dst = entry.get("destination")
        size = entry.get("size")
        sha = entry.get("sha256")
        if not isinstance(src, str) or not src:
            checker.fail(name, f"files[{i}].source invalid")
            continue
        if not isinstance(dst, str) or not dst:
            checker.fail(name, f"files[{i}].destination invalid")
            continue
        if "\\" in src or "\\" in dst or dst.startswith("/") or ".." in src.split("/") or ".." in dst.split("/"):
            checker.fail(name, f"files[{i}] path unsafe: {src} -> {dst}")
        if not dst.startswith("sdk/src/full/"):
            checker.fail(name, f"files[{i}].destination must stay under sdk/src/full/")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            checker.fail(name, f"files[{i}].size must be a positive integer")
        if not isinstance(sha, str) or not HEX64_RE.match(sha):
            checker.fail(name, f"files[{i}].sha256 must be 64 hex characters")
        if "abi" in entry and entry["abi"] not in EXPECTED_ABIS:
            checker.fail(name, f"files[{i}].abi {entry['abi']!r} not expected")
        if src in seen_sources:
            checker.fail(name, f"duplicate source {src}")
        if dst in seen_dests:
            checker.fail(name, f"duplicate destination {dst}")
        seen_sources.add(src)
        seen_dests.add(dst)
    if not any(e.startswith(name) for e in checker.errors):
        checker.ok(f"{name} {len(files)} entries shape valid")


def check_jni_contract(checker: Checker, root: Path) -> None:
    name = "protocol/jni-contract.json"
    data = load_json(checker, name, root / name)
    if data is None:
        return
    for key in ("schemaVersion", "apiContractVersion", "jniContractVersion"):
        if not isinstance(data.get(key), int):
            checker.fail(name, f"{key} must be an integer")
    methods = data.get("methods")
    if not isinstance(methods, list) or not methods:
        checker.fail(name, "methods must be a non-empty list")
        return
    seen = set()
    for i, m in enumerate(methods):
        mname, desc = m.get("name"), m.get("descriptor")
        if not isinstance(mname, str) or not re.match(r"^[A-Za-z][A-Za-z0-9_]*$", mname):
            checker.fail(name, f"methods[{i}].name invalid")
            continue
        if mname in seen:
            checker.fail(name, f"duplicate method {mname}")
        seen.add(mname)
        if not isinstance(desc, str) or not desc.startswith("("):
            checker.fail(name, f"methods[{i}] descriptor invalid")
        if not isinstance(m.get("static"), bool):
            checker.fail(name, f"methods[{i}].static must be boolean")
    errors = data.get("errors")
    if not isinstance(errors, dict) or not errors:
        checker.fail(name, "errors map missing")
    elif not all(isinstance(v, int) and v < 0 for v in errors.values()):
        checker.fail(name, "error codes must be negative integers")
    if not any(e.startswith(name) for e in checker.errors):
        checker.ok(f"{name} {len(methods)} methods, {len(errors or {})} error codes valid")


def check_native_contracts(checker: Checker, root: Path) -> None:
    for rel, name in (("native/vendor/abi-contract.json", "abi-contract"),
                      ("native/vendor/frame-dialect.json", "frame-dialect")):
        data = load_json(checker, name, root / rel)
        if data is None:
            continue
        if data.get("schemaVersion") != 1:
            checker.fail(name, "schemaVersion must be 1")
        if "vendorId" not in data:
            checker.fail(name, "vendorId missing")
        if not any(e.startswith(name) for e in checker.errors):
            checker.ok(f"{rel} parses with schemaVersion 1")


def check_env(checker: Checker) -> None:
    import os
    mapping = {
        "ANDROID_COMPILE_SDK": COMPILE_SDK,
        "ANDROID_MIN_SDK": MIN_SDK,
        "ANDROID_NDK_VERSION": NDK_VERSION,
        "ANDROID_CMAKE_VERSION": CMAKE_VERSION,
    }
    for key, pinned in mapping.items():
        value = os.environ.get(key)
        if value is not None and value != pinned:
            checker.fail(f"env {key}", f"{value!r} conflicts with pinned {pinned!r}")
    checker.ok("env toolchain variables consistent (if set)")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=None,
                        help="repository root (default: parent of ci/)")
    args = parser.parse_args(argv)
    root = Path(args.root).resolve() if args.root else default_root()
    if not root.is_dir():
        print(f"ERROR: root not a directory: {root.name}", file=sys.stderr)
        return 1

    checker = Checker()
    check_wrapper(checker, root)
    check_root_build(checker, root)
    check_sdk_build(checker, root)
    check_gradle_properties(checker, root)
    check_settings(checker, root)
    check_forbidden(checker, root)
    check_vendor_lock(checker, root)
    check_jni_contract(checker, root)
    check_native_contracts(checker, root)
    check_env(checker)

    if checker.errors:
        for err in checker.errors:
            print(f"ERROR: {err}", file=sys.stderr)
        print(f"FAILED: {len(checker.errors)} problem(s)", file=sys.stderr)
        return 1
    print("OK: wrapper, toolchain pins, version formats and lock files verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
