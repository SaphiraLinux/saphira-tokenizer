# C validation state (independent of broken-Rust parity)

## Correctness
- Selftest 1550/1550: plain `-O0`, ASan+UBSan, and `-O2` builds. Same code.
- C == Rust on **4613/4613 non-empty valid-UTF-8 documents**: 0 id-diffs,
  0 errors. (237 apparent "errors" were empty-document counting artefacts.)
- C == HF `tokenizers` == Rust on 10 valid-text probes (ASCII, whitespace,
  multibyte, emoji, code, contractions).
- Phase-6 valid-UTF-8 hash `c8fa1aba...` bit-identical (Rust side untouched).
- Forensic fixtures CONT/CONTR/NOLL match the contract in spans AND ids.
- Byte-map verified 0 mismatches vs `bytes_to_unicode` over 80..FF.

## Known divergence (upstream defect, not C)
54/4821 (110/8921 expanded) vs corrected Rust, ALL inside the documented
`80..BF` defect band, none pure ASCII. C follows the frozen contract
(byte-alphabet fallback); Rust assembles continuation-looking bytes. Recorded
with reproducers `89 88 81` -> U+9201 and `BD 9B 9A` -> U+D6DA. C does not
chase these by design.

## Speed (smoke, not a benchmark)
2 MB slice of OWT, 326 docs, shared box on powersave governor, approximate:
- C scalar `-O0`, hex input path, single-thread: **~0.98 MB/s** input
- C scalar `-O2`, hex input path, single-thread: **~2.68 MB/s** input
- Rust release wheel, batch, 32 threads, SIMD:       **~7.23 MB/s** input

So the unoptimised oracle runs at roughly 1/7th of the shipping Rust on this
box, and `-O2` alone (no source change) nearly triples it. The hex CLI roughly
doubles input bytes plus decode cost, so a binary input path is the obvious
first honest gain. No optimisation work until exactness is frozen, and
exactness is not frozen while the upstream defect stands.

## Thread pinning (i9-13900K, bible 119 MB)
- 24 physical, no HT (8P-even + 16E): **603 MB/s** (best)
- 16 E-cores only: 366 MB/s
- 16 P-threads (HT on): 338 MB/s (SMT contention loses to E-cores here)
- 24 threads unpinned: ~306 MB/s on OWT slice
- 32 threads: drops (E-core SMT + power limits)

## Reverted optimizations (proven wrong or negative)
- Pretoken memo v1 (open addressing): 7x slowdown. Cause: double hashing,
  unbounded probes on a filling table, L1-hostile size.
- Whole-pretoken vocab fast path: CHANGED OUTPUT (BPE does not necessarily
  reproduce whole-pretoken vocab entries; a lower-rank inner merge can block
  the full merge). Caught by checksum before shipping. Reverted.
- PGO (-fprofile-use from OWT): slower on bible (452 vs 603). Overfitted;
  not used.
