#!/usr/bin/env python3
"""Generate the local magnitude cubin table only from validated sweep artifacts."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

EXPECTED = {
    16: "e9176c7d4923520852a5ab52b70a520e9f4f8a6a89ca4307bbaeb01fec74843c",
    32: "dacba2c3732a31566f4fd516b9581c7828503d122bdd16417076ea444ab633fd",
    48: "a4748ba26d085db86e0ebfe5fc326dda24a2ec34beface3c3fe2959c4e0d09eb",
    64: "75054b41d03dfa489e09dc62f760c01fe8bce210497c18489363128802444762",
}
EXPECTED_SIZE = 38432
SOURCE_TEXT = 36992
SOURCE_SHARED = 7776
SOURCE_REGS = 39
SLOT_SIZE = 39968
SOURCE_FNV1A64 = 0x9642092DEF23B3DF

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sweep_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    payloads: dict[int, bytes] = {}
    for px, expected_hash in EXPECTED.items():
        source = args.sweep_dir / f"magnitude-display-{px}px.cubin"
        data = source.read_bytes()
        actual_hash = sha256(data)
        if len(data) != EXPECTED_SIZE:
            raise SystemExit(f"{source}: expected {EXPECTED_SIZE} bytes, got {len(data)}")
        if actual_hash != expected_hash:
            raise SystemExit(f"{source}: SHA-256 mismatch: {actual_hash} != {expected_hash}")
        payloads[px] = data

    out = [
        "// Generated from validated calibrated magnitude-only cubins; do not commit provider-derived payloads.",
        "#pragma once",
        "",
        "struct MagnitudeCubinVariant {",
        "  unsigned display_threshold_px;",
        "  unsigned source_text;",
        "  unsigned source_shared;",
        "  unsigned source_regs;",
        "  unsigned slot_size;",
        "  unsigned long long source_fnv1a64;",
        "  unsigned size;",
        "  const unsigned char* data;",
        "};",
        "",
    ]
    for idx, px in enumerate(EXPECTED):
        data = payloads[px]
        out.append(f"static const unsigned char kMagnitudeCubin{idx}[] = {{")
        for pos in range(0, len(data), 16):
            chunk = data[pos:pos + 16]
            out.append("  " + ",".join(f"0x{byte:02x}" for byte in chunk) + ",")
        out.extend(["};", ""])

    out.append("static const MagnitudeCubinVariant kMagnitudeCubins[] = {")
    for idx, px in enumerate(EXPECTED):
        out.append(
            f"  {{{px}u, {SOURCE_TEXT}u, {SOURCE_SHARED}u, {SOURCE_REGS}u, {SLOT_SIZE}u, "
            f"0x{SOURCE_FNV1A64:016x}ull, sizeof(kMagnitudeCubin{idx}), kMagnitudeCubin{idx}}},"
        )
    out.extend(["};", ""])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out), encoding="utf-8")
    print(args.output)
    print(f"validated {len(payloads)} cubins; generated {args.output.stat().st_size} bytes")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
