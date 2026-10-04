# Benchmarks: saphira-tokenizer C vs Rust vs HF

## Methodology (so nobody has to trust us)

- **Hardware**: `homer`, i9-13900K (8P+16E, 32 threads), AVX2+VNNI, no AVX-512,
  single NUMA node, governor `performance`, box shared (26 users, load ~7).
  All figures are ranges/medians over repeats, not bests. Contention causes
  real spread; we report it instead of hiding it.
- **Corpora**:
  - `OWT-20M`: 20 MB slice of OpenWebText, ~3000 long lines (diverse).
  - `bible-119M`: 1,231,328 verses / 117.6 MB, short lines, highly repetitive.
  - `HF-2M`: 2 MB slice (HF is too slow to run on more; that itself is data).
- **Tokenizer**: GPT-2/r50k fixture (vocab 50257, merges 50000), same file all sides.
- **Metric**: input MB/s (raw bytes in / wall time). Checksums (sum of all ids)
  must match across implementations or the run is void, not faster.
- **Rust**: release wheel, `encode_batch`, 32 threads. **HF**: `tokenizers`
  Python, per-text `encode` (its natural API). **C**: `gt_bench`, binary path.

## Correctness (gates the speed table — a fast wrong run is void)

| check | result |
|---|---|
| Selftest | 1680/1680 (`-O0`, ASan+UBSan, `-O2`) |
| C vs Rust, 4613 non-empty valid-UTF-8 docs | 0 id-diffs, 0 errors |
| C vs HF vs Rust, valid-text probes | agree everywhere |
| Bible full set (1.23M verses) | 28,913,671 tokens, checksum `106370442169` identical C and Rust |
| Phase-6 valid-UTF-8 hash | `c8fa1aba…` bit-identical |
| Known divergences | 54/4821 vs corrected Rust, all in the documented upstream `80..BF` defect band; 192/4821 vs pristine upstream (width + `80..BF`) |

## Speed

| implementation | OWT-20M | bible-119M | HF-2M |
|---|---|---|---|
| C 1t | ~19 MB/s | ~25 MB/s | — |
| C 24t (unpinned) | **~272 MB/s** (median) | — | — |
| C 24 physical, no HT | — | **~314 MB/s** (median; 437–603 spread) | — |
| Rust 32t | ~101–121 MB/s | **574–618 MB/s** | — |
| HF Python | — | — | 4.5 MB/s |

In GB/s (the unit their page uses): C peaks ≈0.31 GB/s here; Rust ≈0.12–0.62
depending on corpus; HF ≈0.0045.

## Reading the table honestly

1. **Corpus shape dominates.** Rust beats C on bible (cache-friendly short
   repetitive verses: their pretoken cache earns its keep) and loses 3× on
   OWT (diverse long lines: our per-byte speed wins). Neither "C is faster"
   nor "Rust is faster" is true without naming the corpus. Anyone reporting
   one number without the corpus is marketing, not measuring — including us
   if we did.
2. **Threading**: C peaks at 24 threads (no-HT physical bests unpinned);
   32 drops. P-cores with HT lose to E-cores here (SMT contention).
3. **HF is off the chart slow** (4.5 MB/s) because of per-document Python
   overhead, not tokenization. Every "× faster than HF" claim in this space
   is mostly a claim about leaving Python out of the loop.
4. **Vs Gigatoken's published 24.53 GB/s** (144 dedicated EPYC cores): different
   hardware class, not comparable from here. Per-core we do ~13 MB/s vs their
   ~170 MB/s — an 11× gap explained by their SIMD splitter + working cache +
   full-power server silicon vs our scalar + shared powersave box. Their number
   is plausible; we cannot verify the absolute from here and don't claim to.

## Training speed (200 KB OWT, vocab 2000)

| implementation | wall | note |
|---|---|---|
| C `gt_train` | 0.087 s, single-threaded | learns 1744 merges |
| Rust `train_bpe` | 0.074 s wall (parallel) | same counts; first-5 merges agree |
| Bible 119 MB, vocab 45498 | 65 s, C single-threaded | 45,242 merges |

C-trained models load in Rust with identical ids, and vice versa.
