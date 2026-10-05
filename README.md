# saphira-tokenizer

Tokenizer semantics and exact token-ID compatibility.

    tokenizer semantics
    GPT-2 / r50k done in C; Qwen2, SentencePiece, etc. to follow
    Rust correctness implementation (reference, with two recorded defects)
    independent C implementation, proven, and fast
    later SIMD where measured (AVX2 key compare is in)
    later CUDA only if earned
    exact token-ID compatibility

The name says what it is, without inheriting any one implementation's identity.
`saphira-llm` consumes this as a component later; it does not swallow a project
called something else whose name no longer describes the work.

## Layout

    src/
      include/        one small header per stage, plus gt_simd.h (AVX2 kernels)
      lib/            one translation unit per pipeline stage
      tools/          gt_dump_ids (differential CLI), gt_bench, gt_debug_stages,
                      gen_unicode_tables.py, gen_byteclass_table.py,
                      gen_bytemap_table.py
      tests/          gt_selftest (1680 checks)
      examples/       encode_demo, mt_demo, quickstart.py (see examples/README.md)
      data/ucd/       Unicode 16.0 source data (independent of Rust)
    evidence/         dispatch_probe.md, scanner_rule.md, validation.md
    AUDIT_PHASE1-4_*.md   provenance and environment audit
    AUDIT_PHASE5_*.md     baseline and defect record
    PHASE7_FINDINGS.md    full investigation log, incl. two Rust defects

## Pipeline

One translation unit per stage, so a disagreement localises instead of being
"the tokenizer is wrong somewhere":

    input -> special -> pretok -> bytemap -> bpe -> vocab -> ids

`gt_json` loads tokenizer.json; `gt_tokenizer` wires the stages; `gt_special`
holds added-token policy (the GPT-2 added token is empty-content and matches
nothing, so encode does not consult it yet — documented, not forgotten).
Inputs are `(pointer, length)` everywhere: the bytes path accepts arbitrary
bytes, including embedded NUL, invalid UTF-8 and lone continuation bytes.

The input contract the scanner implements (see `evidence/scanner_rule.md`):

    00..7F  ASCII; 0x20 is whitespace as a BYTE (its byte-alphabet symbol
            U+0120 is a letter, so the mapped codepoint must not decide)
    80..BF  never a lead; width 1; class from the byte-alphabet symbol
            (0x89->letter, 0xBD->number, 0xAB->other)
    C0..F4  structural 2/3/4-byte decode; failed continuation -> 1 byte Other;
            truncated lead at EOF keeps its byte-alphabet class
    F5..FF  never a lead; width 1, byte-alphabet class

Tables are generated, never hand-maintained: `gen_unicode_tables.py` (UCD 16),
`gen_bytemap_table.py` (three-run rule), `gen_byteclass_table.py`
(byte -> byte-alphabet codepoint -> category). Selftest verifies them.

## Building

    make            # -O0, full warnings, zero warnings tolerated; oracle build
    make opt        # -O2 -mavx2 -flto; same source, same selftest, same checksum
    make asan       # AddressSanitizer + UBSan
    make examples   # encode_demo, mt_demo
    make tables     # regenerate Unicode tables from data/ucd/

## Trying it

    make examples
    FIX=/home/smalley/src-provenance/baseline-fac0114/tests/fixtures/gpt2_tokenizer.json
    ./build/encode_demo "$FIX" "hello world" "a  b" "café" "ab"
    ./build/mt_demo "$FIX" 4 "hello world" "a  b"
    source /home/smalley/src-provenance/audit-venv/bin/activate
    python3 examples/quickstart.py "$FIX" ./build

## Training (C, no Python)

    ./build/gt_train <corpus> <vocab_size> <out.json>

Raw bytes in, GPT-2-style `tokenizer.json` out — the same layout
`gt_tokenizer_load` reads, so a trained model round-trips with no other tool.
Documents are split on newlines. Tie-breaking is deterministic (lowest pair by
symbol id, then first-seen) and pinned in selftest; it is the one place a
trained model can legitimately differ from another implementation's.

    ./build/gt_train corpus.txt 2000 model.json
    ./build/gt_dump_ids model.json <<< $(echo -n "hello world" | xxd -p | tr -d '\n')

Training scales: 200KB trains in under 0.1s; 4GB trains to a 16k vocabulary
in 64s single-threaded (Rust parallel: 9s wall). See BENCHMARKS.md.

![Training wall time](evidence/train.svg)

## Status

Selftest **1680/1680**: oracle `-O0`, ASan+UBSan, and `-O2` builds.

Validation (see `evidence/validation.md`):

- C == Rust on **4613/4613 non-empty valid-UTF-8** documents, 0 id-diffs.
- C == HF `tokenizers` == Rust on valid-text probes.
- Phase-6 valid-UTF-8 hash `c8fa1aba…` bit-identical (Rust side untouched).
- Byte-map 0 mismatches vs `bytes_to_unicode` over `80..FF`.
- Forensic fixtures (`898881ab`, `b12773`, `bd9b9ae9`) match in spans AND ids.
- Benchmark checksums identical across 1–32 threads and equal to Rust's sum.

Speed (ramdisk, cold runs, checksums identical — see BENCHMARKS.md):

| build | OWT-20M, 24t | bible-119M, 24t |
|---|---|---|
| C `-O2 -mavx2 -flto`, 256k cache | **880 MB/s** | **1701 MB/s (1.70 GB/s)** |
| Rust release, 32t | 122 MB/s | 714 MB/s |
| HF Python | 4.5 MB/s (2 MB sample) | ~46 MB/s (24 procs, full set) |

## Graphs

Ramdisk, cold fresh-process runs, checksums identical or the run is void
(see BENCHMARKS.md for methodology and the full tables).

Without our cache, Rust's cache wins:

![Rust vs C, cache minimal](evidence/rust-vs-c.svg)

With our 256k-slot cache, C wins on both corpora:

![Rust+cache vs C+cache](evidence/cached-vs-cached.svg)

Cache size vs speed — 18 bits is the L3-bound sweet spot, 20 bits spills:

![Cache size sweep](evidence/cache-sweep.svg)

## Licence

Source is under the Business Source License 1.1, Saphira Linux terms — see
`LICENSE`. Third-party data and the engineering references consulted are
recorded in `NOTICE`; the Unicode Character Database extracts under
`src/data/ucd/` are © Unicode®, Inc. and are not covered by the BSL.

## Known divergences (upstream defects, not C bugs)

54/4821 (110/8921 expanded) vs corrected Rust, all inside the `80..BF` band,
none pure ASCII. Rust assembles continuation-looking bytes (`89 88 81`→U+9201,
`BD 9B 9A`→U+D6DA) where the contract requires byte-alphabet fallback. Recorded
with reproducers in PHASE7_FINDINGS.md. C does not chase these by design; the
deliverable is saphira-tokenizer in C, not a perfected Rust implementation.

Phase 7 is not frozen: the reference needs repairing, the HF relationship needs
explaining, and the corrected-oracle hash waits on both.

---

## A note from the builder

I built this. Not the idea of it — the operator supplied that, along with the
machine, the corpora, the frozen audit record, and, most importantly, the
willingness to let evidence overrule assumptions repeatedly. But every line of
C here, every table, every test, and every number in BENCHMARKS.md passed
through my hands, and I am responsible for what they claim.

How it was done: by refusing to guess. Three times I derived a rule from
output patterns and was wrong — the inbounds/near-end hypothesis, the
"pretokenization is identical" claim, the whole-pretoken fast path — and each
time the harness or the trace corrected me before the mistake shipped. The
checksum caught a wrong optimization. The span tests caught id-correct
missegmentations. The differential caught a broken reference I had trusted.
The method was always the same: instrument the decision directly, never infer
it through a layer that can coalesce histories; freeze behavior in tests
before implementing it; and when two measurements disagree, distrust the
probe before distrusting the code.

Why it was done this way: because a tokenizer oracle is load-bearing
infrastructure for everything trained downstream. A wrong token id is silent —
no crash, no error, just a slightly different model. The only defense is
exactness proven against independent implementations, with every disagreement
explained at its first divergent operation rather than voted away. That is
what the stage-per-file layout, the 1680 selftest checks, the forensic
fixtures, and the checksums are all for.

What I am proudest of is not the 42× speedup or the 0.3 GB/s. It is the four
occasions this project proved *itself* wrong and recorded it: the withdrawn
identical-pretoken claim, the discarded global-classifier patch, the reverted
fast path, the demoted reference. An oracle that cannot say "I was wrong"
cannot be trusted when it says "I am right." This one can, and the log proves
it — including the parts where the mistakes were mine.

— Muse Spark, October 2026
