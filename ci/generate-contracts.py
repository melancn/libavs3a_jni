#!/usr/bin/env python3
"""Generate C++ contract headers and a machine-readable report from the
frozen JSON contracts (ABI layout, frame dialect, JNI contract).

Deterministic output: no timestamps, sorted JSON keys, atomic file replacement.
Exit 0 on success; a contract that claims ready=true without complete verified
fields/evidence, or any structural inconsistency, exits non-zero.
Standard library only.
"""

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

VALID_ABIS = ("arm64-v8a", "armeabi-v7a", "host-test")
VENDOR_ABIS = ("arm64-v8a", "armeabi-v7a")
POINTER_BYTES = {"arm64-v8a": 8, "armeabi-v7a": 4}
OFFSET_SENTINEL = -1  # invalid offset marker; 0 is a legitimate offset and must never be a stand-in
STORAGE_SIZES = {"int16": 2, "int32": 4}
REQUIRED_ENTRY_POINTS = ("Avs3AllocDecoder", "Avs3InitDecoder", "Avs3Decode",
                         "Avs3DecoderDestroy", "ResetBitstream")
HEX64_RE = re.compile(r"^[0-9a-fA-F]{64}$")


class ContractError(Exception):
    pass


def fail(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)


def load_json(path: Path, label: str) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except FileNotFoundError:
        raise ContractError(f"{label} not found: {path.name}")
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"{label} unreadable/invalid JSON ({path.name}): {exc}")
    if not isinstance(data, dict):
        raise ContractError(f"{label} must be a JSON object")
    if data.get("schemaVersion") != 1:
        raise ContractError(f"{label} schemaVersion must be 1, got {data.get('schemaVersion')!r}")
    return data


def is_int(v) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


# ---------------------------------------------------------------------------
# ABI contract validation (each ABI read independently; no cross-ABI inheritance)
# ---------------------------------------------------------------------------

def validate_field(name: str, spec: dict, abi: str, pointer_bytes: int,
                   state_bytes: int) -> None:
    offset, size = spec.get("offset"), spec.get("size")
    storage = spec.get("storage")
    if not is_int(offset) or offset < 0:
        raise ContractError(f"{abi}.{name}: invalid offset {offset!r}")
    if not is_int(size) or size <= 0:
        raise ContractError(f"{abi}.{name}: invalid size {size!r}")
    if storage == "pointer":
        expect = pointer_bytes
    elif storage == "pointerArray":
        count = spec.get("count")
        if not is_int(count) or count <= 0:
            raise ContractError(f"{abi}.{name}: pointerArray needs positive count")
        expect = count * pointer_bytes
    elif storage in STORAGE_SIZES:
        expect = STORAGE_SIZES[storage]
    else:
        raise ContractError(f"{abi}.{name}: unknown storage {storage!r}")
    if size != expect:
        raise ContractError(
            f"{abi}.{name}: size {size} inconsistent with storage {storage} (expected {expect})")
    if offset + size > state_bytes:
        raise ContractError(
            f"{abi}.{name}: range [{offset},{offset + size}) exceeds decoderStateBytes {state_bytes}")
    verified = spec.get("verified")
    if not isinstance(verified, bool):
        raise ContractError(f"{abi}.{name}: verified must be boolean")
    evidence = spec.get("evidence")
    if verified:
        if not isinstance(evidence, list) or not evidence or \
                not all(isinstance(e, str) and e for e in evidence):
            raise ContractError(f"{abi}.{name}: verified field requires non-empty evidence list")
    if "callerWrites" in spec and not isinstance(spec["callerWrites"], bool):
        raise ContractError(f"{abi}.{name}: callerWrites must be boolean")


def validate_abi_entry(abi: str, entry: dict, contract: dict) -> None:
    if not isinstance(entry, dict):
        raise ContractError(f"abis.{abi} must be an object")
    pointer_bytes = entry.get("pointerBytes")
    if pointer_bytes != POINTER_BYTES[abi]:
        raise ContractError(f"{abi}.pointerBytes {pointer_bytes!r} != expected {POINTER_BYTES[abi]}")
    state_bytes = entry.get("decoderStateBytes")
    if not is_int(state_bytes) or state_bytes <= 0:
        raise ContractError(f"{abi}.decoderStateBytes invalid")
    fields = entry.get("fields")
    if not isinstance(fields, dict) or not fields:
        raise ContractError(f"{abi}.fields must be a non-empty object")
    for name, spec in fields.items():
        validate_field(name, spec, abi, pointer_bytes, state_bytes)

    # No overlap between declared field ranges.
    ranges = sorted((s["offset"], s["offset"] + s["size"], n) for n, s in fields.items())
    for (_, end_a, na), (start_b, _, nb) in zip(ranges, ranges[1:]):
        if start_b < end_a:
            raise ContractError(f"{abi}: fields {na} and {nb} overlap")

    opaque = entry.get("opaqueRanges", [])
    if not isinstance(opaque, list):
        raise ContractError(f"{abi}.opaqueRanges must be a list")
    for rng in opaque:
        off, size = rng.get("offset"), rng.get("size")
        if not is_int(off) or not is_int(size) or off < 0 or size <= 0 or off + size > state_bytes:
            raise ContractError(f"{abi}: invalid opaque range {rng}")
        for s_off, s_size, s_name in ((f["offset"], f["size"], n) for n, f in fields.items()):
            if s_off < off + size and off < s_off + s_size:
                raise ContractError(f"{abi}: opaque range {off}+{size} overlaps field {s_name}")

    bitstream = contract.get("bitstream")
    if not isinstance(bitstream, dict):
        raise ContractError("contract.bitstream missing")
    bsize = bitstream.get("size")
    payload = bitstream.get("payload", {})
    cursor = bitstream.get("cursor", {})
    if entry.get("bitstreamStateBytes") != bsize:
        raise ContractError(f"{abi}.bitstreamStateBytes != bitstream.size")
    if entry.get("payloadCapacityBytes") != payload.get("bytes"):
        raise ContractError(f"{abi}.payloadCapacityBytes != bitstream.payload.bytes")
    if entry.get("bitCursorOffset") != cursor.get("offset"):
        raise ContractError(f"{abi}.bitCursorOffset != bitstream.cursor.offset")
    if not (is_int(bsize) and is_int(payload.get("bytes")) and is_int(cursor.get("bytes"))
           and payload["bytes"] + cursor["bytes"] <= bsize
           and cursor["offset"] + cursor["bytes"] <= bsize):
        raise ContractError(f"{abi}: bitstream ranges inconsistent")

    entry_points = entry.get("entryPoints")
    if not isinstance(entry_points, dict):
        raise ContractError(f"{abi}.entryPoints missing")
    for sym in REQUIRED_ENTRY_POINTS:
        rec = entry_points.get(sym)
        if not isinstance(rec, dict):
            raise ContractError(f"{abi}.entryPoints.{sym} missing")
        try:
            diff = int(rec["symbolValue"], 16) - int(rec["codeAddress"], 16)
        except (KeyError, ValueError):
            raise ContractError(f"{abi}.entryPoints.{sym}: symbolValue/codeAddress not hex strings")
        expected_diff = 1 if pointer_bytes == 4 else 0  # Thumb bit0 preserved on ARMv7
        if diff != expected_diff:
            raise ContractError(f"{abi}.entryPoints.{sym}: symbol/code delta {diff} != {expected_diff}")

    if not isinstance(entry.get("callingConventionVerified"), bool):
        raise ContractError(f"{abi}.callingConventionVerified must be boolean")

    required = contract.get("requiredFields")
    if not isinstance(required, list) or not required:
        raise ContractError("contract.requiredFields must be a non-empty list")
    verified_with_evidence = {
        n for n, s in fields.items()
        if s.get("verified") is True and isinstance(s.get("evidence"), list) and s["evidence"]
    }
    missing = [n for n in required if n not in verified_with_evidence]
    if entry.get("ready") is True:
        if contract.get("requiredFieldsResolved") is not True:
            raise ContractError(f"{abi}: ready=true but requiredFieldsResolved is not true")
        if missing:
            raise ContractError(f"{abi}: ready=true but required fields lack verification/evidence: {missing}")
        if not entry.get("callingConventionVerified"):
            raise ContractError(f"{abi}: ready=true but callingConventionVerified is false")
    if "ready" in entry and not isinstance(entry["ready"], bool):
        raise ContractError(f"{abi}.ready must be boolean")
    if not isinstance(contract.get("functions", {}), dict):
        raise ContractError("contract.functions must be an object")


def validate_abi_contract(contract: dict) -> dict:
    """Validate both vendor ABI entries independently; return per-ABI readiness."""
    abis = contract.get("abis")
    if not isinstance(abis, dict):
        raise ContractError("contract.abis must be an object")
    for abi in VENDOR_ABIS:
        if abi not in abis:
            raise ContractError(f"contract.abis missing entry {abi}")
        validate_abi_entry(abi, abis[abi], contract)
    extra = [a for a in abis if a not in VENDOR_ABIS]
    if extra:
        raise ContractError(f"unexpected abi entries: {extra}")
    return {abi: bool(abis[abi].get("ready")) for abi in VENDOR_ABIS}


# ---------------------------------------------------------------------------
# Frame dialect validation
# ---------------------------------------------------------------------------

LAYOUT_ID_OFFSET = 1  # pcm layout id == channelConfig + 1 (MONO=1, STEREO=2, ...)


def validate_channel_configurations(dialect: dict, bitrates: dict) -> None:
    configs = dialect.get("channelConfigurations")
    if not isinstance(configs, list) or not configs:
        raise ContractError("dialect.channelConfigurations must be a non-empty list")
    seen = set()
    for i, entry in enumerate(configs):
        if not isinstance(entry, dict):
            raise ContractError(f"channelConfigurations[{i}] must be an object")
        cfg = entry.get("channelConfig")
        channels = entry.get("channels")
        lfe = entry.get("lfeFlag")
        fmt = entry.get("decoderFormat")
        table_key = entry.get("bitrateTable")
        layout = entry.get("layout")
        evidence = entry.get("evidence")
        if not is_int(cfg) or cfg != i:
            raise ContractError(
                f"channelConfigurations[{i}]: channelConfig {cfg!r} must equal its index (contiguous from 0)")
        if cfg in seen:
            raise ContractError(f"channelConfigurations: duplicate channelConfig {cfg}")
        seen.add(cfg)
        if not is_int(channels) or channels <= 0 or channels > 24:
            raise ContractError(f"channelConfigurations[{i}]: invalid channels {channels!r}")
        if lfe not in (0, 1):
            raise ContractError(f"channelConfigurations[{i}]: lfeFlag must be 0 or 1")
        if not is_int(fmt) or fmt < 0 or fmt > 4:
            raise ContractError(f"channelConfigurations[{i}]: decoderFormat must be 0..4")
        if not isinstance(layout, str) or not re.match(r"^[A-Z][A-Z0-9_]*$", layout):
            raise ContractError(f"channelConfigurations[{i}]: invalid layout name {layout!r}")
        if table_key is not None and table_key not in bitrates:
            raise ContractError(
                f"channelConfigurations[{i}]: bitrateTable {table_key!r} not in bitrateIndexTables")
        if fmt == 2 and table_key is None:
            # Undecodable-but-representable layouts are allowed (documented vendor gap),
            # but they must carry an explanatory note.
            if not entry.get("note"):
                raise ContractError(
                    f"channelConfigurations[{i}]: null bitrateTable requires a note")
        if not isinstance(evidence, list) or not evidence or \
                not all(isinstance(e, str) and e for e in evidence):
            raise ContractError(f"channelConfigurations[{i}]: non-empty evidence list required")
    modes = dialect.get("headerModes")
    mode_cfgs = modes[0].get("channelConfigurations") if modes else None
    expected = [c["channelConfig"] for c in configs]
    if mode_cfgs is not None and sorted(mode_cfgs) != expected:
        raise ContractError(
            "dialect.headerModes[0].channelConfigurations must match channelConfigurations entries")


def crc_reflect16(table: list, data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc = ((crc << 8) ^ table[(crc >> 8) & 0xFF] ^ byte) & 0xFFFF
    return crc


def validate_layout_cross_contract(dialect: dict, jni: dict) -> None:
    """Every channel configuration layout must have a pcmLayouts entry whose
    id equals channelConfig + LAYOUT_ID_OFFSET (the id mapping used by the
    generated headers, native session metadata, and the Java PcmLayout)."""
    layouts = jni.get("pcmLayouts", {})
    for entry in dialect.get("channelConfigurations", []):
        layout = entry.get("layout")
        expected_id = entry.get("channelConfig") + LAYOUT_ID_OFFSET
        actual_id = layouts.get(layout)
        if not isinstance(actual_id, int) or actual_id != expected_id:
            raise ContractError(
                f"pcmLayouts.{layout}: id {actual_id!r} != channelConfig "
                f"{entry.get('channelConfig')} + {LAYOUT_ID_OFFSET}")


def derive_crc16_table(poly: int) -> list:
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        table.append(crc)
    return table


def validate_dialect(dialect: dict) -> None:
    header_bytes = dialect.get("headerBytes")
    if not is_int(header_bytes) or header_bytes <= 0:
        raise ContractError("dialect.headerBytes invalid")
    total_bits = header_bytes * 8
    modes = dialect.get("headerModes")
    if not isinstance(modes, list) or not modes:
        raise ContractError("dialect.headerModes must be a non-empty list")
    known = dialect.get("knownPrefix", {})
    for mode in modes:
        fields = mode.get("fields")
        if not isinstance(fields, list) or not fields:
            raise ContractError(f"headerMode {mode.get('id')!r}: fields empty")
        ordered = sorted(fields, key=lambda f: f.get("bitOffset", -1))
        cursor = 0
        for f in ordered:
            off, width = f.get("bitOffset"), f.get("width")
            if not is_int(off) or not is_int(width) or width <= 0:
                raise ContractError(f"headerMode {mode.get('id')!r}: field {f.get('name')!r} invalid offset/width")
            if off != cursor:
                raise ContractError(
                    f"headerMode {mode.get('id')!r}: field {f.get('name')!r} bitOffset {off} != {cursor} (must tile contiguously)")
            cursor = off + width
        if cursor != total_bits:
            raise ContractError(f"headerMode {mode.get('id')!r}: fields cover {cursor} bits != header {total_bits}")
        by_name = {f["name"]: f for f in fields}
        if "sync" in by_name:
            if by_name["sync"].get("width") != known.get("syncBits") or \
                    by_name["sync"].get("required") != known.get("syncValue"):
                raise ContractError("dialect: sync field inconsistent with knownPrefix")
        if "codecId" in by_name:
            if by_name["codecId"].get("width") != known.get("followingCodecIdBits") or \
                    by_name["codecId"].get("required") != known.get("followingCodecIdValue"):
                raise ContractError("dialect: codecId field inconsistent with knownPrefix")

    table = dialect.get("sampleRateIndexTable")
    if not isinstance(table, list) or len(table) != 9 or not all(is_int(v) and v > 0 for v in table):
        raise ContractError("dialect.sampleRateIndexTable must be 9 positive int entries")
    bitrates = dialect.get("bitrateIndexTables")
    if not isinstance(bitrates, dict) or not bitrates:
        raise ContractError("dialect.bitrateIndexTables missing")
    for key in ("mono", "stereo"):
        if key not in bitrates:
            raise ContractError(f"dialect.bitrateIndexTables.{key} missing")
    for key, tbl in bitrates.items():
        if not isinstance(tbl, list) or len(tbl) != 16 or not all(is_int(v) and v >= 0 for v in tbl):
            raise ContractError(f"dialect.bitrateIndexTables.{key} must be 16 non-negative int entries")
    validate_channel_configurations(dialect, bitrates)
    crc = dialect.get("crcRules")
    if not isinstance(crc, dict):
        raise ContractError("dialect.crcRules missing")
    ctable = crc.get("table")
    if not isinstance(ctable, list) or len(ctable) != 256 or \
            not all(is_int(v) and 0 <= v <= 0xFFFF for v in ctable):
        raise ContractError("dialect.crcRules.table must be 256 uint16 entries")
    if ctable != derive_crc16_table(crc.get("poly", 0)):
        raise ContractError("dialect.crcRules.table does not match the declared poly")
    if crc_reflect16(ctable, b"123456789") != int(crc.get("check123456789", ""), 16):
        raise ContractError("dialect.crcRules.table fails the documented 123456789 checksum")
    if crc_reflect16(ctable, b"") != int(crc.get("empty", "ffff"), 16):
        raise ContractError("dialect.crcRules.table fails the empty-input check")
    policy = dialect.get("sdkSafetyPolicy")
    if not isinstance(policy, dict) or not is_int(policy.get("maxPayloadBits")) or \
            not is_int(policy.get("maxAdmittedFrameBytes")):
        raise ContractError("dialect.sdkSafetyPolicy missing required bounds")
    if not isinstance(dialect.get("verified"), bool):
        raise ContractError("dialect.verified must be boolean")
    if dialect.get("verified") is True and not dialect.get("verificationLevel"):
        raise ContractError("dialect verified=true requires non-empty verificationLevel")


# ---------------------------------------------------------------------------
# JNI contract
# ---------------------------------------------------------------------------

def validate_jni(jni: dict) -> None:
    for key in ("apiContractVersion", "jniContractVersion", "pcmMetadataLongs",
                "encodedFrameMetadataLongs"):
        if not is_int(jni.get(key)):
            raise ContractError(f"jni-contract.{key} must be an integer")
    methods = jni.get("methods")
    if not isinstance(methods, list) or not methods:
        raise ContractError("jni-contract.methods must be a non-empty list")
    for i, m in enumerate(methods):
        if not isinstance(m.get("name"), str) or not re.match(r"^[A-Za-z][A-Za-z0-9_]*$", m["name"]):
            raise ContractError(f"jni-contract.methods[{i}].name invalid")
        if not isinstance(m.get("descriptor"), str) or not m["descriptor"].startswith("("):
            raise ContractError(f"jni-contract.methods[{i}].descriptor invalid")
        if not isinstance(m.get("static"), bool):
            raise ContractError(f"jni-contract.methods[{i}].static must be boolean")
    for section in ("queueResults", "receiveResults", "errors", "pcmLayouts"):
        mapping = jni.get(section)
        if not isinstance(mapping, dict) or not mapping:
            raise ContractError(f"jni-contract.{section} must be a non-empty object")
        for name, value in mapping.items():
            if not re.match(r"^[A-Z][A-Z0-9_]*$", name) or not is_int(value):
                raise ContractError(f"jni-contract.{section}.{name} invalid")
    layouts = jni.get("pcmLayouts", {})
    if len(set(layouts.values())) != len(layouts):
        raise ContractError("jni-contract.pcmLayouts values must be unique")
    if any(v < 0 for v in layouts.values()):
        raise ContractError("jni-contract.pcmLayouts values must be non-negative")


# ---------------------------------------------------------------------------
# Header generation
# ---------------------------------------------------------------------------

def guard(name: str) -> str:
    return "AVS3A_" + name.upper().replace("-", "_").replace(".", "_") + "_H"


def macro_name(identifier: str) -> str:
    upper = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", identifier).upper()
    if not re.match(r"^[A-Z0-9_]+$", upper):
        raise ContractError(f"identifier {identifier!r} cannot be mapped to a macro name")
    return upper


def render_lines(lines) -> str:
    return "\n".join(lines) + "\n"


def gen_status_header(jni: dict) -> str:
    g = guard("status_generated")
    out = [
        "// Generated by ci/generate-contracts.py - DO NOT EDIT.",
        f"#ifndef {g}", f"#define {g}", "",
        f'#define AVS3A_NATIVE_CLASS "{jni["nativeClass"]}"',
        f'#define AVS3A_API_CONTRACT_VERSION {jni["apiContractVersion"]}',
        f'#define AVS3A_JNI_CONTRACT_VERSION {jni["jniContractVersion"]}',
        f'#define AVS3A_PCM_METADATA_LONGS {jni["pcmMetadataLongs"]}',
        f'#define AVS3A_ENCODED_FRAME_METADATA_LONGS {jni["encodedFrameMetadataLongs"]}',
        f'#define AVS3A_JNI_METHOD_COUNT {len(jni["methods"])}',
        "",
        "// PCM layout ids (id = channelConfig + 1; see PcmLayout in the Java SDK)",
    ]
    for name, value in sorted(jni.get("pcmLayouts", {}).items(), key=lambda kv: kv[1]):
        out.append(f"#define AVS3A_PCM_LAYOUT_{name} {value}")
    out += [
        "",
        "// Queue/receive results",
    ]
    for name, value in sorted(jni["queueResults"].items()):
        out.append(f"#define AVS3A_QUEUE_{name} {value}")
    for name, value in sorted(jni["receiveResults"].items()):
        out.append(f"#define AVS3A_RECEIVE_{name} {value}")
    out.append("")
    out.append("// Error codes")
    for name, value in sorted(jni["errors"].items()):
        out.append(f"#define AVS3A_ERR_{name} {value}")
    out.append("")
    out.append("// JNI method descriptors (static native)")
    for m in jni["methods"]:
        out.append(f'#define AVS3A_JNI_DESC_{m["name"]} "{m["descriptor"]}"')
    out += ["", f"#endif // {g}"]
    return render_lines(out)


def gen_vendor_header(abi: str, entry, abi_contract: dict, abi_ready: bool) -> str:
    g = guard("vendor_contract_generated")
    out = [
        "// Generated by ci/generate-contracts.py - DO NOT EDIT.",
        f"#ifndef {g}", f"#define {g}", "",
        "#define AVS3A_VENDOR_ABI_NAME \"%s\"" % abi,
        f"#define AVS3A_VENDOR_ID \"{abi_contract.get('vendorId', '')}\"",
        f"#define AVS3A_VENDOR_OFFSET_UNKNOWN {OFFSET_SENTINEL}",
        "// 1 only when the static contract for this exact ABI is fully verified;",
        "// host-test never declares vendor runtime readiness.",
        f"#define AVS3A_VENDOR_ABI_READY {1 if abi_ready else 0}",
    ]
    if abi not in VENDOR_ABIS or entry is None or not abi_ready:
        # Safe skeleton: everything unknown, sentinel offsets, never 0 as a guess.
        required = abi_contract.get("requiredFields", [])
        out += [
            "#define AVS3A_VENDOR_POINTER_BYTES 0",
            "#define AVS3A_VENDOR_DECODER_STATE_BYTES 0",
            "#define AVS3A_VENDOR_PAYLOAD_CAPACITY_BYTES 0",
            "#define AVS3A_VENDOR_BITSTREAM_STATE_BYTES 0",
            "#define AVS3A_VENDOR_BIT_CURSOR_OFFSET AVS3A_VENDOR_OFFSET_UNKNOWN",
            "",
            "// Layout intentionally not exposed (unverified contract or host-test).",
        ]
        for name in required:
            macro = macro_name(name)
            out.append(f"#define AVS3A_VENDOR_OFF_{macro} AVS3A_VENDOR_OFFSET_UNKNOWN")
            out.append(f"#define AVS3A_VENDOR_SIZE_{macro} {OFFSET_SENTINEL}")
        out += ["", f"#endif // {g}"]
        return render_lines(out)

    fields = entry["fields"]
    pointer_bytes = entry["pointerBytes"]
    out += [
        f"#define AVS3A_VENDOR_POINTER_BYTES {pointer_bytes}",
        f"#define AVS3A_VENDOR_DECODER_STATE_BYTES {entry['decoderStateBytes']}",
        f"#define AVS3A_VENDOR_PAYLOAD_CAPACITY_BYTES {entry['payloadCapacityBytes']}",
        f"#define AVS3A_VENDOR_BITSTREAM_STATE_BYTES {entry['bitstreamStateBytes']}",
        f"#define AVS3A_VENDOR_BIT_CURSOR_OFFSET {entry['bitCursorOffset']}",
        "",
        "// Decoder-state field layout (offsets verified in abi-contract.json).",
    ]
    for name in sorted(fields):
        spec = fields[name]
        macro = macro_name(name)
        verified = spec.get("verified") is True and isinstance(spec.get("evidence"), list) and spec["evidence"]
        off = spec["offset"] if verified else OFFSET_SENTINEL
        size = spec["size"] if verified else OFFSET_SENTINEL
        out.append(f"#define AVS3A_VENDOR_OFF_{macro} {off}")
        out.append(f"#define AVS3A_VENDOR_SIZE_{macro} {size}")
        out.append(f'#define AVS3A_VENDOR_STORAGE_{macro} "{spec["storage"]}"')
    out += ["", "// Vendor entry point symbols"]
    for sym in sorted(entry.get("entryPoints", {})):
        out.append(f'#define AVS3A_VENDOR_SYM_{macro_name(sym)} "{sym}"')
    out += ["", f"#endif // {g}"]
    return render_lines(out)


def gen_dialect_header(dialect: dict) -> str:
    g = guard("frame_dialect_generated")
    ready = dialect.get("verified") is True
    out = [
        "// Generated by ci/generate-contracts.py - DO NOT EDIT.",
        f"#ifndef {g}", f"#define {g}", "",
        "#include <cstdint>",
        "",
        f"#define AVS3A_FRAME_DIALECT_ID \"{dialect.get('vendorId', '')}\"",
        f"#define AVS3A_FRAME_DIALECT_READY {1 if ready else 0}",
        f"#define AVS3A_FRAME_HEADER_BYTES {dialect['headerBytes']}",
        f"#define AVS3A_FRAME_SYNC_BITS {dialect['knownPrefix']['syncBits']}",
        f"#define AVS3A_FRAME_SYNC_VALUE {dialect['knownPrefix']['syncValue']}",
        f"#define AVS3A_FRAME_CODEC_ID_BITS {dialect['knownPrefix']['followingCodecIdBits']}",
        f"#define AVS3A_FRAME_CODEC_ID_VALUE {dialect['knownPrefix']['followingCodecIdValue']}",
        f"#define AVS3A_FRAME_SAMPLES_PER_CHANNEL {dialect['observedFrameSamplesPerChannel']}",
        f"#define AVS3A_FRAME_MAX_BYTES {dialect['maxFrameBytes']}",
        f"#define AVS3A_FRAME_MIN_BYTES {dialect['minFrameBytes']}",
        f"#define AVS3A_MAX_PAYLOAD_BITS {dialect['sdkSafetyPolicy']['maxPayloadBits']}",
        f"#define AVS3A_MAX_ADMITTED_FRAME_BYTES {dialect['sdkSafetyPolicy']['maxAdmittedFrameBytes']}",
        "",
        "namespace avs3a {",
        "",
        f"inline constexpr int32_t kSampleRateIndexTable[{len(dialect['sampleRateIndexTable'])}] = {{",
        ", ".join(str(v) for v in dialect["sampleRateIndexTable"]) + "};",
        "",
    ]
    for key in sorted(dialect["bitrateIndexTables"]):
        tbl = dialect["bitrateIndexTables"][key]
        out.append(f"inline constexpr int32_t kBitrateIndexTable_{key.upper()}[{len(tbl)}] = {{")
        out.append(", ".join(str(v) for v in tbl) + "};")
        out.append("")
    configs = dialect.get("channelConfigurations", [])
    out += [
        "struct ChannelConfiguration {",
        "    int32_t channel_config;",
        "    int32_t channels;",
        "    int32_t lfe_flag;",
        "    int32_t decoder_format;",
        "    int32_t layout_id;",
        "    const int32_t* bitrate_table;  // null when the vendor build has no table",
        "};",
        "",
        f"inline constexpr int32_t kChannelConfigurationCount = {len(configs)};",
        f"inline constexpr ChannelConfiguration kChannelConfigurations[kChannelConfigurationCount] = {{",
    ]
    for entry in configs:
        table_ref = "nullptr"
        if entry["bitrateTable"] is not None:
            table_ref = f"kBitrateIndexTable_{entry['bitrateTable'].upper()}"
        out.append(
            "    {{{}, {}, {}, {}, {}, {}}},".format(
                entry["channelConfig"], entry["channels"], entry["lfeFlag"],
                entry["decoderFormat"], entry["channelConfig"] + LAYOUT_ID_OFFSET, table_ref))
    out += ["};", ""]
    mode = dialect["headerModes"][0]
    out.append(f"inline constexpr int32_t kHeaderBitOffset_{mode['id'].upper().replace('-', '_')}[{len(mode['fields'])}] = {{")
    out.append(", ".join(str(f["bitOffset"]) for f in mode["fields"]) + "};")
    out += ["", "inline constexpr uint16_t kCrc16VendorTable[256] = {"]
    table = dialect["crcRules"]["table"]
    for i in range(0, 256, 16):
        out.append("    " + ", ".join(str(v) for v in table[i:i + 16]) + ",")
    out += [
        "};",
        "",
        "} // namespace avs3a",
        "",
        f"#endif // {g}",
    ]
    return render_lines(out)


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(content, encoding="utf-8", newline="\n")
    os.replace(tmp, path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--abi", required=True, choices=VALID_ABIS)
    parser.add_argument("--abi-contract", required=True, type=Path)
    parser.add_argument("--dialect-contract", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jni-contract", default=None, type=Path,
                        help="default: protocol/jni-contract.json at repo root")
    args = parser.parse_args(argv)

    try:
        script_dir = Path(__file__).resolve().parent
        jni_path = args.jni_contract or (script_dir.parent / "protocol" / "jni-contract.json")
        abi_contract = load_json(args.abi_contract, "abi-contract")
        dialect = load_json(args.dialect_contract, "frame-dialect")
        jni = load_json(jni_path, "jni-contract")

        abi_readiness = validate_abi_contract(abi_contract)  # both ABIs validated independently
        validate_dialect(dialect)
        validate_jni(jni)
        validate_layout_cross_contract(dialect, jni)

        abi = args.abi
        entry = abi_contract["abis"].get(abi) if abi in VENDOR_ABIS else None
        # host-test must never declare vendor runtime readiness; a vendor ABI is
        # only ready when its own (not the other ABI's) contract is fully verified.
        abi_ready = bool(entry and entry.get("ready")) and abi in VENDOR_ABIS

        required = abi_contract.get("requiredFields", [])
        fields = entry["fields"] if entry else {}
        missing_required = [
            n for n in required
            if n not in fields or not (fields[n].get("verified") is True
                                       and isinstance(fields[n].get("evidence"), list)
                                       and fields[n]["evidence"])
        ]
        dialect_ready = dialect.get("verified") is True

        out_dir = args.output
        headers = {
            "status_generated.h": gen_status_header(jni),
            "vendor_contract_generated.h": gen_vendor_header(abi, entry, abi_contract, abi_ready),
            "frame_dialect_generated.h": gen_dialect_header(dialect),
        }
        for name in sorted(headers):
            atomic_write(out_dir / name, headers[name])

        report = {
            "abiContractSchemaVersion": abi_contract["schemaVersion"],
            "abiStaticReady": {a: abi_readiness[a] for a in VENDOR_ABIS},
            "apiContractVersion": jni["apiContractVersion"],
            "dialectReady": bool(dialect_ready),
            "dialectSchemaVersion": dialect["schemaVersion"],
            "dialectVerificationLevel": dialect.get("verificationLevel", ""),
            "errorCount": len(jni["errors"]),
            "generatedFiles": sorted(headers),
            "jniContractVersion": jni["jniContractVersion"],
            "methodCount": len(jni["methods"]),
            "missingRequiredFields": missing_required,
            "runtimeValidation": abi_contract.get("runtimeValidation", "NOT_RUN"),
            "selectedAbi": abi,
            "selectedAbiRequiredFieldCount": len(required),
            "sourceContractScope": abi_contract.get("scope", ""),
            "vendorAbiReadyDeclared": 1 if abi_ready else 0,
            "vendorAbiReadySource": "host-test never inherits or declares vendor readiness"
                                    if abi not in VENDOR_ABIS else "per-abi static contract",
            "vendorId": abi_contract.get("vendorId", ""),
        }
        report["inputSha256"] = {
            "abiContract": sha256_file(args.abi_contract),
            "dialectContract": sha256_file(args.dialect_contract),
            "jniContract": sha256_file(jni_path),
        }
        atomic_write(out_dir / "contract-report.json",
                     json.dumps(report, indent=2, sort_keys=True) + "\n")
    except ContractError as exc:
        fail(str(exc))
        return 1
    print(f"OK: generated contracts for abi={args.abi} "
          f"(vendorReady={report['vendorAbiReadyDeclared']}, dialectReady={report['dialectReady']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
