#!/usr/bin/env python3
"""Emit SVG bar charts for the benchmark comparison. No dependencies.
Run: python3 tools/mkgraphs.py  (writes evidence/*.svg)
Data are measured numbers, hardcoded with source notes below.
"""
import pathlib

OUT = pathlib.Path("/home/smalley/.local/src/saphira-gigatoken/evidence")

# Measured on homer, i9-13900K, performance governor, ramdisk (/dev/shm),
# cold fresh-process runs. C 24t (OWT) / 24-physical-no-HT (bible);
# Rust 32t batch. Checksums identical everywhere or the run is void.
OWT_C_MIN, OWT_C_MAX = 93.0, 880.0
BIB_C_MIN, BIB_C_MAX = 153.0, 1701.0
OWT_RUST, BIB_RUST = 122.0, 714.0
HF = 4.5

SWEEP_BITS = [8, 11, 14, 16, 18, 20]
SWEEP_OWT = [93.0, 351.0, 614.0, 809.0, 880.0, 770.0]
SWEEP_BIB = [182.0, 562.0, 1161.0, 1710.0, 1732.0, 1546.0]


def svg_bar(path, title, groups, series, colors, unit="MB/s", log=False):
    W, H, pad = 640, 360, 60
    bw = (W - 2 * pad) // (len(groups) * len(series) + len(groups))
    mx = max(max(v) for v in series.values()) * 1.12
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="sans-serif">']
    s.append(f'<rect x="0" y="0" width="{W}" height="{H}" fill="#0d1117" rx="8"/>')
    FG, GRID = "#ffffff", "#8b949e"
    s.append(f'<text fill="{FG}" x="{W//2}" y="24" text-anchor="middle" font-size="16">{title}</text>')
    s.append(f'<line x1="{pad}" y1="{H-pad}" x2="{W-20}" y2="{H-pad}" stroke="{GRID}"/>')
    for gi, g in enumerate(groups):
        for si, name in enumerate(series):
            v = series[name][gi]
            h = (H - 2 * pad) * v / mx
            x = pad + gi * (len(series) * bw + bw) + si * bw
            y = H - pad - h
            s.append(f'<rect x="{x}" y="{y:.0f}" width="{bw-2}" height="{h:.0f}" fill="{colors[si]}"/>')
            s.append(f'<text fill="{FG}" x="{x+(bw-2)//2}" y="{H-pad-h-4:.0f}" text-anchor="middle" font-size="11">{v:g}</text>')
        cx = pad + gi * (len(series) * bw + bw) + (len(series) * bw) // 2
        s.append(f'<text fill="{FG}" x="{cx}" y="{H-pad+18}" text-anchor="middle" font-size="12">{g}</text>')
    for si, name in enumerate(series):
        s.append(f'<rect x="{W-190}" y="{40+si*20}" width="12" height="12" fill="{colors[si]}"/>')
        s.append(f'<text fill="{FG}" x="{W-174}" y="{50+si*20}" font-size="12">{name}</text>')
    s.append(f'<text fill="{FG}" x="{pad}" y="{H-8}" font-size="10">input MB/s, ramdisk, cold; checksums identical or void</text>')
    s.append('</svg>')
    (OUT / path).write_text("\n".join(s) + "\n")


BLUE, ORANGE, GREEN, RED = "#2b7bbb", "#ee7b25", "#3aa655", "#cc3333"

svg_bar("rust-vs-c.svg", "Rust vs C (C cache minimal)",
        ["OWT-20M", "bible-119M"],
        {"Rust (cached)": [OWT_RUST, BIB_RUST],
         "C (minimal cache)": [OWT_C_MIN, BIB_C_MIN]},
        [BLUE, ORANGE])

svg_bar("cached-vs-cached.svg", "Rust+cache vs C+cache (256k slots)",
        ["OWT-20M", "bible-119M"],
        {"Rust+cache": [OWT_RUST, BIB_RUST],
         "C+cache": [OWT_C_MAX, BIB_C_MAX]},
        [BLUE, GREEN])

svg_bar("cache-sweep.svg", "C throughput vs cache size (bits)",
        ["8", "11", "14", "16", "18", "20"],
        {"OWT-20M": SWEEP_OWT, "bible-119M": SWEEP_BIB},
        [ORANGE, GREEN])

print("wrote", sorted(p.name for p in OUT.glob("*.svg")))
