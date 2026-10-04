#!/usr/bin/env python3
"""quickstart.py — the smallest possible C-vs-Rust differential.

Usage:
    source /home/smalley/src-provenance/audit-venv/bin/activate
    python3 examples/quickstart.py <tokenizer.json> <c-build-dir>

Example:
    python3 examples/quickstart.py \
        /home/smalley/src-provenance/baseline-fac0114/tests/fixtures/gpt2_tokenizer.json \
        /home/smalley/.local/src/saphira-gigatoken/src/build

Encodes a fixed battery with the C CLI (hex in, ids out) and with the pinned
Rust wheel, and prints MATCH / DIFFER per case. Anything marked DIFFER is a
real divergence worth investigating; see PHASE7_FINDINGS.md for the known set.
"""
import subprocess
import sys

from gigatoken.gigatoken_rs import BPETokenizer

CASES = [
    b"",
    b"ab",
    b"hello",
    b"Hello, world!",
    b"a b",
    b"a  b",
    b"hello world",
    b"caf\xc3\xa9",
    b"a\x00b",
    b"a\xffb",
    bytes(range(256)),
    b"\x89\x88\x81\xab",
    b"\xb1's",
    b"\xbd\x9b\x9a\xe9",
]


def main():
    fixture, build = sys.argv[1], sys.argv[2]
    tok = BPETokenizer.from_hf(fixture)
    payload = ("\n".join(d.hex() for d in CASES) + "\n").encode()
    p = subprocess.run(
        [f"{build}/gt_dump_ids", fixture],
        input=payload, capture_output=True)
    if p.returncode != 0:
        print("C tool failed:")
        print(p.stderr.decode()[:2000])
        return 1
    lines = p.stdout.decode().split("\n")[:len(CASES)]
    bad = 0
    for raw, line in zip(CASES, lines):
        rust = tok.encode(raw).tolist()
        if line.startswith("ERR"):
            c = None
        elif not line.strip():
            c = [] if len(raw) == 0 else None
        else:
            c = [int(x) for x in line.split()]
        mark = "MATCH " if c == rust else "DIFFER"
        if c != rust:
            bad += 1
        print(f"{mark} {raw!r:28} rust={rust} c={c}")
    print(f"\n{len(CASES) - bad}/{len(CASES)} match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
