#!/usr/bin/env python3
"""Verify device test evidence binds to the exact candidate release AAR.

Compares AAR SHA, SDK version, sourceCommit, API/JNI contract versions, real
process ABI (pointerBytes 8 for arm64-v8a, 4 for armeabi-v7a), loaded JNI/
vendor/model hashes against the AAR and vendor lock, fixture manifest
integrity, per-fixture sample math and lifecycle test results. Detects
missing fields, ABI duplication or ABI coverage gaps. Exit 0 only when every
report passes; otherwise non-zero. Standard library only.
"""

import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
POINTER_BYTES = {"arm64-v8a": 8, "armeabi-v7a": 4}
BRIDGE_LIB = "libavs3a_jni.so"
MANIFEST_STATUS_BAD = {None, "", "NOT_READY", "NOT_PROVIDED", "NOT_RUN", "UNKNOWN"}
REPORT_KEYS = ("schemaVersion", "status", "runId", "timestampUtc", "aarSha256",
               "sdkVersion", "sourceCommit", "apiContractVersion", "jniContractVersion",
               "fixtureManifestSha256", "fixtures", "lifecycleTests", "runnerProvenance")
PROCESS_KEYS = ("abi", "pointerBytes", "pageSize", "androidApi", "buildFingerprint")
LOADED_KEYS = ("jniSha256", "vendorSha256", "modelSha256", "jniBuildId", "vendorBuildId")
RUNNER_KEYS = ("id", "logSha256")
FIXTURE_REPORT_KEYS = ("id", "result", "decodedSamplesPerChannel", "primingSamples",
                       "trailingSamples", "channels", "sampleRateHz",
                       "maxAbsErrorLsb", "rmsErrorLsb", "actualPcmSha256")
CONFIG_KEYS = ("profile", "channelConfig", "sourceBits", "neuralType",
               "sampleRateHz", "bitrateBps")


class EvidenceError(Exception):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path, label: str):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"{label} unreadable/invalid ({path.name}): {exc}")


def parse_elf(data: bytes):
    if len(data) < 52 or data[:4] != b"\x7fELF":
        return None
    ei_class, little = data[4], data[5] == 1
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
    build_id = None
    if e_shoff and e_shentsize and e_shnum:
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            if off + e_shentsize > len(data):
                return {"machine": f"0x{e_machine:x}", "buildId": None}
            if ei_class == 2:
                f = struct.unpack_from(endian + "IIQQQQIIQQ", data, off)
            else:
                f = struct.unpack_from(endian + "IIIIIIIIII", data, off)
            if f[1] != 4:
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
    return {"machine": f"0x{e_machine:x}", "buildId": build_id}


def read_aar_jni(aar_path: Path):
    libs = {}
    with zipfile.ZipFile(aar_path) as zf:
        for name in zf.namelist():
            if name.startswith("jni/") and name.endswith(f"/{BRIDGE_LIB}"):
                abi = name.split("/")[1]
                data = zf.read(name)
                elf = parse_elf(data)
                if elf is None or elf["buildId"] is None:
                    raise EvidenceError(f"cannot parse ELF/Build ID from {name} in AAR")
                libs[abi] = {"sha256": hashlib.sha256(data).hexdigest(),
                             "buildId": elf["buildId"], "elf": elf}
    if not libs:
        raise EvidenceError("AAR contains no jni/<abi> bridge library")
    return libs


def is_nonempty_str(v) -> bool:
    return isinstance(v, str) and v != ""


def is_int(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def require_fields(checker, obj: dict, keys, prefix: str) -> None:
    for key in keys:
        if key not in obj:
            checker.error(f"{prefix}.{key}: missing")
        elif obj[key] is None:
            checker.error(f"{prefix}.{key}: null")
        elif isinstance(obj[key], str) and obj[key] == "":
            checker.error(f"{prefix}.{key}: empty")


class Checker:
    def __init__(self):
        self.errors = []

    def error(self, msg):
        self.errors.append(msg)

    def ok(self, msg):
        print(f"[ok] {msg}")


def validate_fixture_manifest(checker, fm: dict):
    if fm.get("schemaVersion") != 1:
        checker.error("fixture-manifest.schemaVersion must be 1")
    if fm.get("status") in MANIFEST_STATUS_BAD:
        checker.error(f"fixture-manifest.status {fm.get('status')!r} is not usable evidence")
    configs = fm.get("requiredConfigurations")
    if not isinstance(configs, list) or not configs:
        checker.error("fixture-manifest.requiredConfigurations must be non-empty")
        configs = []
    config_set = set()
    for cfg in configs:
        if not isinstance(cfg, dict):
            checker.error(f"requiredConfigurations entry not an object: {cfg!r}")
            continue
        if any(cfg.get(k) is None or not is_int(cfg.get(k)) for k in CONFIG_KEYS):
            checker.error(f"configuration incomplete/non-integer: {cfg}")
            continue
        config_set.add(tuple(cfg[k] for k in CONFIG_KEYS))
    fixtures = fm.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        checker.error("fixture-manifest.fixtures must be non-empty (empty fixtures can never accept)")
        return {}
    by_id = {}
    for fx in fixtures:
        fid = fx.get("id") if isinstance(fx, dict) else None
        label = f"fixture {fid!r}" if is_nonempty_str(fid) else "fixture <missing-id>"
        if not is_nonempty_str(fid):
            checker.error(f"{label}: id missing")
            continue
        if fid in by_id:
            checker.error(f"{label}: duplicate id")
            continue
        cfg = fx.get("configuration")
        if not isinstance(cfg, dict) or tuple(cfg.get(k) for k in CONFIG_KEYS) not in config_set:
            checker.error(f"{label}: configuration not listed in requiredConfigurations")
            continue
        enc = fx.get("encoded", {})
        ref = fx.get("reference", {})
        prov = fx.get("provenance", {})
        delay = fx.get("delay", {})
        comp = fx.get("comparison", {})
        if not is_nonempty_str(enc.get("sha256")) or not HEX64_RE.match(enc.get("sha256", "")):
            checker.error(f"{label}: encoded.sha256 invalid")
        if not is_int(enc.get("bytes")) or enc.get("bytes", 0) <= 0:
            checker.error(f"{label}: encoded.bytes must be positive")
        if not is_int(enc.get("frameCount")) or enc.get("frameCount", 0) <= 0:
            checker.error(f"{label}: encoded.frameCount must be positive")
        if not is_nonempty_str(ref.get("sha256")) or not HEX64_RE.match(ref.get("sha256", "")):
            checker.error(f"{label}: reference.sha256 invalid")
        rpc, channels = ref.get("samplesPerChannel"), ref.get("channels")
        if not is_int(rpc) or rpc <= 0 or not is_int(channels) or channels <= 0:
            checker.error(f"{label}: reference samplesPerChannel/channels invalid")
        elif not is_int(ref.get("bytes")) or ref.get("bytes") != rpc * channels * 2:
            checker.error(f"{label}: reference.bytes must equal samplesPerChannel*channels*2")
        if cfg.get("channelConfig") in (0, 1) and is_int(channels) and channels != cfg["channelConfig"] + 1:
            checker.error(f"{label}: reference.channels {channels} inconsistent with channelConfig")
        if not is_nonempty_str(prov.get("reviewId")):
            checker.error(f"{label}: provenance.reviewId missing (DUT self-generated expectations not allowed)")
        priming, trailing = delay.get("primingSamples"), delay.get("trailingSamples")
        if (priming is None or trailing is None or (is_int(priming) and priming < 0)
                or (is_int(trailing) and trailing < 0)
                or not is_nonempty_str(delay.get("evidencePath"))
                or not is_nonempty_str(delay.get("evidenceSha256"))):
            checker.error(f"{label}: delay evidence incomplete (unknown delay blocks; never default)")
        max_abs, rms = comp.get("maxAbsErrorLsb"), comp.get("rmsErrorLsb")
        if not (is_int(max_abs) and is_int(rms)):
            checker.error(f"{label}: comparison thresholds must be integers")
        elif (max_abs != 0 or rms != 0) and not is_nonempty_str(comp.get("policyReviewId")):
            checker.error(f"{label}: non-zero thresholds require policyReviewId")
        by_id[fid] = fx
    return by_id


def validate_report(checker, report: dict, aar_sha: str, manifest: dict, jni: dict,
                    aar_libs: dict, lock: dict, fixture_by_id: dict,
                    fm_sha: str, seen_abis: dict) -> None:
    base = len(checker.errors)
    require_fields(checker, report, REPORT_KEYS, "report")
    if not isinstance(report.get("process"), dict) or not isinstance(report.get("loadedArtifacts"), dict):
        checker.error("report.process/loadedArtifacts must be objects")
        return
    require_fields(checker, report["process"], PROCESS_KEYS, "report.process")
    require_fields(checker, report["loadedArtifacts"], LOADED_KEYS, "report.loadedArtifacts")
    if not isinstance(report.get("runnerProvenance"), dict):
        checker.error("report.runnerProvenance must be an object")
        return
    require_fields(checker, report["runnerProvenance"], RUNNER_KEYS, "report.runnerProvenance")
    if len(checker.errors) > base:
        return
    if report["status"] != "PASS":
        checker.error(f"report status {report['status']!r} is not PASS")
    if not is_nonempty_str(report["runId"]):
        checker.error("report.runId must be a non-empty string")
    if report["aarSha256"] != aar_sha:
        checker.error("report.aarSha256 does not match the candidate AAR")
    if manifest.get("sdkVersion") is not None and report["sdkVersion"] != manifest["sdkVersion"]:
        checker.error("report.sdkVersion != manifest.sdkVersion")
    if report["sourceCommit"] != manifest.get("sourceCommit"):
        checker.error("report.sourceCommit != manifest.sourceCommit")
    if not COMMIT_RE.match(str(report["sourceCommit"])):
        checker.error("report.sourceCommit must be a 40-hex commit (uncommitted is not evidence)")
    for key, expected in (("apiContractVersion", jni.get("apiContractVersion")),
                          ("jniContractVersion", jni.get("jniContractVersion"))):
        if report.get(key) != expected:
            checker.error(f"report.{key} {report.get(key)!r} != contract {expected!r}")
        if manifest.get(key) is not None and manifest[key] != expected:
            checker.error(f"manifest.{key} != contract version")
    abi = report["process"]["abi"]
    if abi not in POINTER_BYTES:
        checker.error(f"report.process.abi {abi!r} is not a real ARM ABI")
    else:
        if abi in seen_abis:
            checker.error(f"duplicate ABI reports for {abi} (reports must be unique per ABI)")
        seen_abis[abi] = True
        expected_ptr = POINTER_BYTES[abi]
        if report["process"]["pointerBytes"] != expected_ptr:
            checker.error(f"report.process.pointerBytes {report['process']['pointerBytes']!r} "
                          f"!= {expected_ptr} for {abi} (process ABI not genuinely {abi})")
        if not is_int(report["process"]["pageSize"]) or report["process"]["pageSize"] <= 0:
            checker.error("report.process.pageSize must be a positive integer")
        if not is_int(report["process"]["androidApi"]) or report["process"]["androidApi"] < 24:
            checker.error("report.process.androidApi must be >= 24")
    if report["fixtureManifestSha256"] != fm_sha:
        checker.error("report.fixtureManifestSha256 does not match the supplied fixture manifest")
    la = report["loadedArtifacts"]
    if not HEX64_RE.match(la["jniSha256"] or ""):
        checker.error("report.loadedArtifacts.jniSha256 invalid")
    elif abi in aar_libs and la["jniSha256"] != aar_libs[abi]["sha256"]:
        checker.error(f"report JNI hash != jni/{abi} bytes in AAR")
    if la["jniBuildId"] and abi in aar_libs and la["jniBuildId"] != aar_libs[abi]["buildId"]:
        checker.error(f"report JNI Build ID != Build ID in AAR for {abi}")
    if abi:
        dec_sha = next((e["sha256"] for e in lock["files"] if e.get("abi") == abi), None)
        if dec_sha is None:
            checker.error(f"vendor lock has no decoder entry for {abi}")
        elif la["vendorSha256"] != dec_sha:
            checker.error(f"report.vendorSha256 != lock hash for {abi}")
    model_sha = next((e["sha256"] for e in lock["files"] if not e.get("abi")), None)
    if model_sha is not None and la["modelSha256"] != model_sha:
        checker.error("report.modelSha256 != lock model hash")
    if not re.match(r"^[0-9a-fA-F]{8,}$", str(la["vendorBuildId"] or "")):
        checker.error("report.loadedArtifacts.vendorBuildId invalid")
    fixtures = report["fixtures"]
    if not isinstance(fixtures, list) or not fixtures:
        checker.error("report.fixtures must be non-empty")
        fixtures = []
    ids_seen = set()
    for item in fixtures:
        if not isinstance(item, dict):
            checker.error("report fixture entry not an object")
            continue
        fid = item.get("id")
        label = f"report.fixtures[{fid!r}]"
        require_fields(checker, item, FIXTURE_REPORT_KEYS, label)
        if any(item.get(k) is None for k in FIXTURE_REPORT_KEYS):
            continue
        numeric = ("decodedSamplesPerChannel", "primingSamples", "trailingSamples",
                   "channels", "sampleRateHz", "maxAbsErrorLsb", "rmsErrorLsb")
        if not all(is_int(item.get(k)) for k in numeric):
            checker.error(f"{label}: numeric fields must be integers")
            continue
        if fid in ids_seen:
            checker.error(f"{label}: listed twice")
        ids_seen.add(fid)
        if fid not in fixture_by_id:
            checker.error(f"{label}: not present in fixture manifest")
            continue
        if item["result"] != "PASS":
            checker.error(f"{label} result is {item['result']!r}, not PASS")
        mf = fixture_by_id[fid]
        ref, comp = mf["reference"], mf["comparison"]
        needed = item["decodedSamplesPerChannel"] - item["primingSamples"] - item["trailingSamples"]
        if needed != ref.get("samplesPerChannel"):
            checker.error(f"{label}: decoded-priming-trailing ({needed}) "
                          f"!= reference samples ({ref.get('samplesPerChannel')})")
        if item["channels"] != ref.get("channels") or item["sampleRateHz"] != ref.get("sampleRateHz"):
            checker.error(f"{label}: channels/sampleRateHz differ from manifest reference")
        if (item["maxAbsErrorLsb"] > comp.get("maxAbsErrorLsb", 0)
                or item["rmsErrorLsb"] > comp.get("rmsErrorLsb", 0)):
            checker.error(f"{label}: error exceeds manifest comparison thresholds")
        if not HEX64_RE.match(item["actualPcmSha256"] or ""):
            checker.error(f"{label}: actualPcmSha256 invalid")
    lifecycle = report["lifecycleTests"]
    if not isinstance(lifecycle, list) or not lifecycle:
        checker.error("report.lifecycleTests must be non-empty")
    else:
        for t in lifecycle:
            if not isinstance(t, dict) or t.get("result") != "PASS" or not is_nonempty_str(t.get("name")):
                checker.error(f"lifecycle test entry not a PASS with a name: {t!r}")
    if not HEX64_RE.match(report["runnerProvenance"]["logSha256"] or ""):
        checker.error("report.runnerProvenance.logSha256 invalid")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--aar", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--device-report", required=True, type=Path, action="append",
                        help="one per ABI; repeat for each device report")
    parser.add_argument("--fixture-manifest", required=True, type=Path)
    parser.add_argument("--jni-contract", type=Path, default=None)
    parser.add_argument("--lock", type=Path, default=None)
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    checker = Checker()
    try:
        if not args.aar.is_file():
            raise EvidenceError(f"AAR not found: {args.aar.name}")
        jni_path = args.jni_contract or (root / "protocol" / "jni-contract.json")
        lock_path = args.lock or (root / "vendor" / "manifest.lock.json")
        for p in (args.manifest, args.fixture_manifest, jni_path, lock_path):
            if not p.is_file():
                raise EvidenceError(f"required input missing: {p.name}")
        aar_sha = sha256_file(args.aar)
        manifest = load_json(args.manifest, "sdk-manifest")
        jni = load_json(jni_path, "jni-contract")
        lock = load_json(lock_path, "vendor lock")
        if not isinstance(lock.get("files"), list) or not lock["files"]:
            raise EvidenceError("vendor lock has no files")
        fm = load_json(args.fixture_manifest, "fixture-manifest")
        fm_sha = sha256_file(args.fixture_manifest)
        aar_libs = read_aar_jni(args.aar)

        if manifest.get("distribution") != "full":
            checker.error("sdk-manifest.distribution must be full for device evidence")
        for key in ("sdkVersion", "sourceCommit", "apiContractVersion", "jniContractVersion"):
            if manifest.get(key) in (None, ""):
                checker.error(f"sdk-manifest.{key} missing")
        aar_record = manifest.get("aar")
        if isinstance(aar_record, dict) and aar_record.get("sha256") not in (None, aar_sha):
            checker.error("sdk-manifest aar sha256 does not match the supplied AAR")

        fixture_by_id = validate_fixture_manifest(checker, fm)
        seen_abis = {}
        for path in args.device_report:
            report = load_json(path, f"device report {path.name}")
            if not isinstance(report, dict):
                checker.error(f"{path.name}: report must be a JSON object")
                continue
            before = len(checker.errors)
            validate_report(checker, report, aar_sha, manifest, jni, aar_libs, lock,
                            fixture_by_id, fm_sha, seen_abis)
            if len(checker.errors) == before:
                checker.ok(f"{path.name}: abi={report.get('process', {}).get('abi')} "
                           "binds to candidate AAR")
        missing_abis = sorted(set(aar_libs) - set(seen_abis))
        if missing_abis:
            checker.error(f"no device report for AAR ABI(s): {missing_abis}")
    except EvidenceError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if checker.errors:
        for err in checker.errors:
            print(f"ERROR: {err}", file=sys.stderr)
        print(f"FAILED: {len(checker.errors)} problem(s)", file=sys.stderr)
        return 1
    print("OK: device evidence binds to the candidate AAR for all ABIs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
