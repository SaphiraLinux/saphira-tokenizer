#!/usr/bin/env python3
"""Generate C classification tables from the Unicode Character Database.

The GPT-2 pretokenizer needs three predicates on a codepoint:

    \\p{L}   general category Letter  (Lu Ll Lt Lm Lo)
    \\p{N}   general category Number  (Nd Nl No)
    \\s      the White_Space property

This script derives them from the UCD directly and emits
`gt_unicode_tables.c`. It deliberately does NOT read anything from the Rust
implementation: if both implementations agree, that is two independent
derivations meeting, not one copied into the other.

Sources (Unicode 16.0.0, matching the tables the reference crate's ICU4X
2.2.0 ships):

    ucd/extracted/DerivedGeneralCategory.txt
    ucd/PropList.txt

The generated form is a sorted list of inclusive codepoint ranges — readable,
obviously correct, and trivially diffable when the UCD is updated. It is not
packed into bits or otherwise shaped for speed: this is a reference
implementation, and the differential harness is the thing being tested.

Usage:  python3 tools/gen_unicode_tables.py [--ucd DIR] [--out FILE]
"""

from __future__ import annotations

import argparse
import os
import sys

MAX_CP = 0x10FFFF

LETTER_CATEGORIES = {"Lu", "Ll", "Lt", "Lm", "Lo"}
NUMBER_CATEGORIES = {"Nd", "Nl", "No"}


def parse_ranges(path: str, wanted: set[str]) -> list[tuple[int, int]]:
    """Inclusive codepoint ranges whose property/value is in `wanted`."""
    out: list[tuple[int, int]] = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [f.strip() for f in line.split(";")]
            if len(fields) < 2:
                continue
            codes, value = fields[0], fields[1]
            if value not in wanted:
                continue
            if ".." in codes:
                lo_s, hi_s = codes.split("..", 1)
                lo, hi = int(lo_s, 16), int(hi_s, 16)
            else:
                lo = hi = int(codes, 16)
            out.append((lo, hi))
    out.sort()
    return out


def merge(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Coalesce touching or overlapping ranges."""
    merged: list[tuple[int, int]] = []
    for lo, hi in ranges:
        if merged and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
        else:
            merged.append((lo, hi))
    return merged


def emit_ranges(fh, name: str, ranges: list[tuple[int, int]]) -> None:
    fh.write(f"const gt_cp_range {name}[] = {{\n")
    for lo, hi in ranges:
        fh.write(f"    {{0x{lo:04X}, 0x{hi:04X}}},\n")
    fh.write("};\n")
    fh.write(
        f"const size_t {name}_count = "
        f"sizeof({name}) / sizeof({name}[0]);\n\n"
    )


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    default_ucd = os.path.join(here, os.pardir, "data", "ucd")
    default_out = os.path.join(here, os.pardir, "lib", "gt_unicode_tables.c")

    ap = argparse.ArgumentParser()
    ap.add_argument("--ucd", default=os.path.normpath(default_ucd))
    ap.add_argument("--out", default=os.path.normpath(default_out))
    args = ap.parse_args()

    dgc = os.path.join(args.ucd, "DerivedGeneralCategory.txt")
    plist = os.path.join(args.ucd, "PropList.txt")
    for p in (dgc, plist):
        if not os.path.isfile(p):
            print(f"missing UCD file: {p}", file=sys.stderr)
            return 1

    letters = merge(parse_ranges(dgc, LETTER_CATEGORIES))
    numbers = merge(parse_ranges(dgc, NUMBER_CATEGORIES))
    spaces = merge(parse_ranges(plist, {"White_Space"}))

    def has(rs: list[tuple[int, int]], cp: int) -> bool:
        return any(lo <= cp <= hi for lo, hi in rs)

    # Guard against a silently truncated or misparsed UCD. This checks reach,
    # not an upper bound: U+10FFFF is unassigned (Cn), so no class table is
    # expected to cover it, and demanding that would be wrong.
    for label, rs, probes in (
        ("letters", letters, [0x41, 0x7A, 0x4E00, 0x3042, 0xAC00, 0x1D400, 0x2B740]),
        ("numbers", numbers, [0x30, 0x39, 0x660, 0x966, 0x2150, 0xFF10]),
        ("spaces", spaces, [0x09, 0x0A, 0x0D, 0x20, 0x85, 0xA0, 0x2000, 0x3000]),
    ):
        missing = [cp for cp in probes if not has(rs, cp)]
        if missing:
            print(
                f"warning: {label} table does not reach "
                + ", ".join(f"U+{cp:04X}" for cp in missing),
                file=sys.stderr,
            )

    # Independent spot checks against the standard, so a silently truncated or
    # misparsed UCD cannot reach the C build unnoticed.
    assert has(letters, ord("A")) and has(letters, ord("z")), "letters missing ASCII"
    assert not has(letters, ord("1")) and not has(letters, ord(" ")), "letters over-broad"
    assert has(numbers, ord("0")) and has(numbers, ord("9")), "numbers missing ASCII"
    assert not has(numbers, ord("a")), "numbers over-broad"
    assert has(spaces, 0x20) and has(spaces, 0x09) and has(spaces, 0x0A), "space missing"
    assert has(spaces, 0xA0) and has(spaces, 0x3000), "space missing exotic"
    assert not has(spaces, ord("a")), "space over-broad"
    assert has(letters, 0x4E00), "CJK ideograph should be a letter"
    assert has(numbers, 0x0660), "Arabic-Indic digit should be a number"

    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(
            "/* Generated by tools/gen_unicode_tables.py from the Unicode\n"
            " * Character Database 16.0.0. Do not edit by hand.\n"
            " *\n"
            " * Source: ucd/extracted/DerivedGeneralCategory.txt (\\p{L}, \\p{N})\n"
            " *         ucd/PropList.txt (White_Space)\n"
            " *\n"
            " * Plain sorted inclusive ranges. Deliberately unpacked: this is the\n"
            " * reference implementation, and clarity is worth more here than\n"
            " * density. Packing or binarising is a later, measured decision.\n"
            " */\n\n"
            '#include "gt_unicode.h"\n\n'
        )
        fh.write("/* \\p{L}  */\n")
        emit_ranges(fh, "gt_cp_ranges_letter", letters)
        fh.write("/* \\p{N}  */\n")
        emit_ranges(fh, "gt_cp_ranges_number", numbers)
        fh.write("/* White_Space, i.e. \\s  */\n")
        emit_ranges(fh, "gt_cp_ranges_space", spaces)

    print(f"wrote {args.out}")
    print(f"  \\p{{L}} ranges: {len(letters)}")
    print(f"  \\p{{N}} ranges: {len(numbers)}")
    print(f"  White_Space ranges: {len(spaces)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())