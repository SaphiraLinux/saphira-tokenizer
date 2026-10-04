# Phase 7 — scalar C GPT-2/r50k: state and one open question

## What is built and verified

Plain scalar C11, no SIMD, no optimisation, default `-O0` with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
-Wstrict-prototypes -Wmissing-prototypes`. Clean under ASan + UBSan.

Stages are separate translation units so a mismatch localises:
`gt_unicode` (UCD 16 tables + strict decoder) -> `gt_pretok` (GPT-2 pattern)
-> `gt_bytemap` (byte -> mapped codepoint) -> `gt_bpe` (merge loop)
-> `gt_vocab` (sorted array + binary search). `gt_json` loads tokenizer.json;
`gt_tokenizer` wires the stages; `gt_special` holds added-token policy.

- `gt_selftest`: 337 checks, 0 failures (plain and sanitized).
- Differential vs the Rust oracle: **155 mismatches / 4821 documents**, and
  every one is in the random-arbitrary-bytes fuzz set. All valid-UTF-8,
  corpus, whitespace, contraction, punctuation and all-256-bytes cases match.

## Bugs found and fixed along the way (all caught by the harness, not by reading)

1. `gt_buf` was not NUL-terminated while callers `strcmp`'d its contents —
   out-of-bounds read on every JSON key.
2. `vkey_push` indexed the flat key array by entry index instead of a running
   codepoint offset, and sized it by entry count instead of codepoint count.
3. Constructors left `*out` untouched on failure, so a reused caller variable
   kept a freed pointer (use-after-free).
4. Merge-rule parser stored `left_len` as a byte length although keys are
   codepoints, and mis-stepped over multi-byte codepoints.
5. `\s+(?!\S)` never gave back the whitespace run's final character, so
   `"a  b"` merged as one pretoken instead of `"a", " ", " b"`.
6. Branch 4 rescanned from `start` after consuming the optional leading space
   and broke on that space, so `" '"` was two pretokens instead of one.
7. The merge pass wrote `A.off` (the *first* occurrence's offset) instead of the
   offset of the occurrence being merged, aliasing every later occurrence to
   position 0 and corrupting the next round's pair search.

## THE OPEN QUESTION: 155 divergences where the Rust oracle contradicts itself

Minimal case: `b"\xef@ypx"`.

    gigatoken_rs.encode(b"\xef@ypx")  ->  [171, 31, 88, 8416]     # 'y' + 'px'
    gigatoken_rs.encode(b"@ypx")      ->  [31, 4464, 87]          # 'yp' + 'x'
    gigatoken_rs.encode(b"ypx")       ->  [4464, 87]              # 'yp' + 'x'

Evidence that the oracle is the outlier, not this C:

1. **Its own pretokenizer agrees the input is the same.**
   `gigatoken_rs.pretokenizer()` returns exactly `['ï', '@', 'ypx']` for
   `\xef@ypx` and `['@', 'ypx']` for `@ypx`. The `ypx` pretoken is byte-for-byte
   identical in both, yet BPE gives different answers for it. A pure function
   of the pretoken cannot do that.
2. **Canonical BPE ranking says `yp`, not `px`.** In the fixture's merges list
   `"y p"` is index 4208 and `"p x"` is index 8160, with no duplicates, so
   `(y,p)` has strictly lower rank and must merge first. An independent Python
   transcription of the reference `encoder.py` algorithm reproduces this C's
   answer, and HF `tokenizers` also yields `yp`+`x` for the same pretoken.
3. Reproducible and cache-independent: stable across fresh processes and across
   alternating calls in one process.

So on these inputs the C follows the specification and `gigatoken_rs` does not.
That is a defect in the oracle that the frozen audit record has not caught,
because the frozen differential corpus (`capture_ids.py`) contains no
arbitrary-byte documents — every entry in it is valid UTF-8 text.

## Decision needed before freezing the scalar-C result hash

Which definition of "exact" governs the freeze?

- **A. Spec/canonical + HF as authority.** The 155 are oracle bugs; record
  them as a new Phase 6-style defect with minimal repros, and freeze the C hash
  against canonical BPE. This keeps the C honest but means the C is deliberately
  *not* equal to the pinned Rust build on those inputs.
- **B. gigatoken is authoritative by fiat.** The C must reproduce the oracle's
  behaviour, including this. That means reverse-engineering a behaviour that
  contradicts the merges table and the oracle's own pretokenizer — likely a
  cache or table bug in `gigatoken_rs` that would then be replicated in C.
- **C. Fix `gigatoken_rs` first**, re-run the frozen Rust oracle, and only then
  freeze the C against a corrected oracle. Cleanest, but changes the frozen
  baseline and therefore the audit record.

Nothing is committed yet; the outer repo still has zero commits.

---

# Root-cause investigation (continued)

## Frozen evidence

- 155 disagreements frozen to `/home/smalley/src-provenance/phase7/frozen_155.json`,
  sha256 `5b349ff61661689a480840bc1f242c566a8899c53e2225ece58a3e73510c3965`.
- Generator is seeded, so it is byte-reproducible.

## Stage localisation: NOT pretokenization

The encoder's own segmentation API agrees on both sides, so the pretoken fed to
BPE is byte-identical:

    g.pretokenizer(b'\xc3\xaf' b'@' b'ypx')  ->  [b'\xc3\xaf', b'@', b'ypx']
    g.pretokenizer(b'@' b'ypx')              ->  [b'@', b'ypx']

`gigatoken_rs.pretokenized_counts` (the encoder's own path) returns the same
pieces. Yet:

    encode(b'ypx')      -> [4464, 87]        # 'yp' + 'x'   correct
    encode(b'@ypx')     -> [31, 4464, 87]    # correct
    encode(b'\xef@ypx') -> [171, 31, 88, 8416]   # 'y' + 'px'  WRONG

The `ypx` pretoken is identical in all three; only the *surrounding* pretokens
differ. So the divergence is in merge execution / rank lookup / cached-value
retrieval, not in byte handling and not in pretokenization.

## Trigger is a small, closed set of byte values

Sweeping all 256 possible leading bytes in `bytes([b]) + b'@yp'` and checking
whether the trailing `yp` still merges (last token must be 4464):

    breaks the merge: 0xE0 0xE1 0xE2 0xE3 0xEE 0xEF      (6 of 256)
    all others fine:                                    (250 of 256)

Those six bytes are self-mapped to U+00E0..U+00E3, U+00EE, U+00EF
(`à á â ã î ï`) — i.e. 2-byte UTF-8 in the byte-level alphabet.
Neighbouring self-mapped characters such as U+00E4..U+00ED and U+00F0..U+00FF
are also 2-byte and do NOT trigger, so this is not simply "any non-ASCII".

Minimal reproducer (6 bytes):

    encode(b'\xe0@yp') -> wrong ; encode(b'a@yp') -> right

## Shape of the wrong answer

The wrong output is the **unmerged** byte sequence (`88, 79` = `'y'`, `'p'`)
where the merged single token `4464` = `'yp'` is expected. So the merge step
produced no merge at all for that pretoken, as if the pair lookup missed or a
cached value was wrong.

## Ruled out

- Not the SIMD ports: `bpe_merge_symbols_short_avx2` / `_avx512` are dead code on
  x86-64 ("x86-64 stays scalar ON PURPOSE", `src/bpe/tiktoken.rs`), and the live
  `_loadu_si256` path is unaligned-safe regardless.
- Not duplicate merges: the fixture's 50,000 merge strings have zero duplicates.
- Not the cache hit/miss state: a fresh tokenizer per call, and priming with
  `b'ypx'` first, does not change the wrong answer.
- Not nondeterminism: stable across fresh processes and repeated alternating calls.

## Prime suspect (not yet pinned to a line)

`src/bpe/pretoken_cache.rs` — `ShortPretokenCache`, a hand-rolled open-addressing
table with `unsafe impl Send/Sync for Slots`, 32-byte entries, linear probing over
*aligned pairs* (bucket = slots `idx`,`idx+1` with `idx` even), a vocab seed, and
`probe_pair` intended to resolve displacement-0/1 from a single cache line. The
observed symptom (a short pretoken yielding its pre-merge byte tokens, dependent
on which other short pretokens were inserted first) is exactly what a probe/seed
disagreement in that table would produce. Pinning it requires reading the insert
and `probe_pair` paths in full; that is the next step.

## Next steps

1. Read `ShortPretokenCache` insert + `probe_pair` + vocab-seed paths in full and
   pin the exact defect.
2. Fix minimally in `gigatoken_rs`.
3. Re-run the ENTIRE Phase 6 oracle, not just the 155.
4. Emit a new corrected Rust oracle hash; keep `c8fa1aba...` as the forensic
   pre-fix reference only.
5. Require corrected Rust == C scalar across: old corpus, all 155 regressions,
   minimal reproducers, arbitrary-byte corpus, fuzz corpus.
6. Continue Phase 7.

## Integration intent

This tokenizer work is intended eventually to integrate into **saphira-llm**.
The C API is therefore kept clean and separable rather than coupled to the
current standalone package layout: stages are independent translation units
behind small headers, no global state, no knowledge of the CLI or of the Rust
package layout.

## Pre-fix fixture

`/home/smalley/src-provenance/phase7/prefix_cache_defect_fixture.json`
sha256 `94be4273b84768b5d0fa597d380ac4a22ea2db3e0855cdbd465234b0964246de`

Contains: the 6 trigger bytes + control, the full 256-byte sweep, the isolation
trio (`ypx_alone` / `at_ypx` / `ef_at_ypx`), the 6-byte minimal reproducer,
expected canonical IDs from an independent reference, current wrong Rust IDs,
the C's IDs, pretoken segmentation, and the three ordering experiments.
This is forensic evidence, **not** an oracle hash.

Sweep detail: `bytes([b]) + b'@yp'` disagrees for `0xE0 0xE1 0xE2 0xE3 0xEE 0xEF`.
`0x20` also disagrees but for an unrelated, legitimate reason (the optional
leading space in ` ?[^\s\p{L}\p{N}]+` makes `' @'` one pretoken), so the
closed trigger set is the six high bytes.

Wrong answer shape: `[.., 88, 79]` where `[.., 4464]` is expected. `88` and `79`
are the single-byte tokens for `'y'` and `'p'`, i.e. the pair was left **unmerged**
— as if the rank lookup for `(88, 79)` returned "no merge".

## Causality: NOT yet established

`set_max_cache_bytes(0 | 1KiB | 1MiB)` does **not** reproduce a bypass — the
knob only governs resizing, and all six triggers stay wrong at every size. So
the cache has NOT been proved or busted. A genuine bypass is still required:
gate `probe_emit_slow`'s `get_or_slot` read and `probe_emit_chunk`'s `fast`
predicate behind a temporary env var, rebuild the wheel, and re-run the A/B.

Also still open: whether the encoder pretokenizes in the original byte domain
(byte `0xEF` yields the single token `171`, which is only consistent with the
pretoken being `[0xEF]` rather than the byte-mapped `[0xC3, 0xAF]`). If so, the
earlier `pretokenizer()` segmentation evidence was taken on the mapped domain
and does not by itself prove the encoder feeds BPE an identical pretoken — that
needs re-establishing on the correct domain before the cache can be pinned.

---

# ROOT CAUSE PINNED: pretokenizer byte-span segmentation (not the cache)

## Trace evidence (instrumented, env-gated by `GT_TRACE`)

Instrumented `pack_pretoken_key`, `merge_short` seeding, and `PairRankTable::rank`.
For the 6-byte reproducer `b'\xef@yp'`:

    [PACK] bytes=[ef, 40, 79] n=3 mask_lo=0000000000ffffff key=030000000000000000000000007940ef
    [PACK] bytes=[70]         n=1 mask_lo=00000000000000ff key=01000000000000000000000000000070
    [SEED] pretoken=[ef, 40, 79] n=3 remapped=[<171>, <31>, <88>] pair_ranks=true
    [RANK] pair=(171, 31) dense idx=350239 -> MISS
    [RANK] pair=(31, 88)  dense idx=63576  -> MISS
    IDS [171, 31, 88, 79]

Every component below pretokenization is exonerated:

- `pack_mask_halves`: n=1 -> `lo=0xFF`, n=2 -> `lo=0xFFFF`, n=3 -> `lo=0xFFFFFF`.
  Correct. Keys carry the length in the top byte and bytes in the lanes,
  little-endian, and are correct.
- Byte remapping boundary: `remapped=[171, 31, 88]` is exactly
  `br.mapping[0xEF]=171, br.mapping[0x40]=31, br.mapping[0x79]=88`. Correct.
- `PairRankTable::rank(171,31)` -> MISS and `rank(31,88)` -> MISS are both
  correct: neither pair merges. Dense index arithmetic is correct.

## First divergent operation: the pretoken span

The pretoken handed to BPE was **`[0xEF, 0x40, 0x79]` — one 3-byte pretoken** —
where the correct segmentation is `[0xEF]`, `[0x40]`, `[0x79, 0x70]`.

Because `y` and `p` were split across the pretoken boundary, the pair
`(88, 79)` was never presented to the rank table at all. That is why the answer
is the unmerged `[88, 79]` rather than `[4464]`. The rank table never got the
chance to be wrong.

Confirmed on raw bytes — the debug binding and the encoder agree exactly:

    b'\xef@yp'  -> ['ef4079','70']      WRONG
    b'a@yp'     -> ['61','40','7970']   correct
    b'\xe0@yp'  -> ['e04079','70']      WRONG
    b'\xe4@yp'  -> ['e4407970']         misgrouped but harmless (no wrong pair merges)
    b'\xef@ypx' -> ['ef4079','7078']    WRONG

`\xe4@yp` shows the misgrouping is broader than the six answer-changing bytes:
it is also fused into a single 4-byte pretoken. It returns the right answer only
by luck, because no pair spanning the unwanted boundary happens to merge. So
the byte set that changes the OUTPUT (six bytes) is narrower than the byte set
that missegments (at least 0xE0..0xFF).

## Retraction

The earlier claim "pretokenization is identical, therefore the defect is below
pretokenization" was **wrong** and is withdrawn. It came from calling
`pretokenizer()` on the byte-**mapped** alphabet, which takes a different code
path and segments correctly. On raw bytes — the domain the encoder actually
uses — the pretokenizer genuinely misgroups. There is no self-contradiction in
the oracle's outputs and no cache defect.

## Next steps

1. Locate the span-segmentation defect in `src/pretokenize/` (the `fast`
   scanner's high-byte handling versus `reference`), given that 0xE0..0xFF are
   the 2- and 3-byte UTF-8 lead bytes and the pretokenizer must classify raw
   bytes in the byte-level alphabet, not decode UTF-8.
2. Minimal regression on the exact primitive: a pretokenizer span test asserting
   `b'\xef@yp'` segments to `[ef] [40] [7970]`, plus the 256-byte sweep.
3. Fix once, rebuild once, rerun: 6 triggers, 256 sweep, 155 divergences,
   Phase-6 corpus, C differential, randomized byte corpus.
4. Only then freeze a corrected Rust/C oracle hash.

---

# Fix landed, verification PARTIAL — not frozen

Commit `e749f8f` on `audit/phase6` in `baseline-fac0114`.
Trace/archaeology isolated on `debug/forensic-trace` @ `c2cf96b` (not merged).

## Root cause

`decode_cp` in `src/pretokenize/fast/mod.rs` inferred a codepoint width from
the lead byte's RANGE alone and never checked that the following bytes were
real continuation bytes (`0b10xxxxxx`). Both `decode_cp_inbounds` and
`decode_cp_near_end` did this.

On a byte-level tokenizer the input is arbitrary bytes, so a byte that merely
RESEMBLES a lead swallowed its neighbours: `0xEF` followed by an ASCII byte
was decoded as a 3-byte char, and `advance_pos` consumed three bytes on input
containing no such character. The pretoken span moved, splitting letter runs
that must stay together. BPE was correct throughout; it was handed wrong
boundaries.

Fix: width is a proposal, validated against real continuation bytes. Not-a-lead
or lead-without-continuations consumes exactly 1 byte and classifies `Other`.
Continuation bytes in lead position (`0x80..0xBF`) propose no width;
`0xF5..0xFF` are never leads. Valid UTF-8 keeps real widths.

## Verified green

- Phase-6 valid-UTF-8 oracle: `c8fa1aba...` **bit-identical**. Correctly scoped:
  it remains the valid-UTF-8 oracle hash and is unchanged; it was never evidence
  that arbitrary-byte tokenization worked.
- Full Phase-6 suite: **117 passed / 0 failed / 20 ignored** (was 112 + 5 new).
- Original C differential corpus (4821 docs): **0 mismatches**, C id-hash
  `4155265f...` unchanged — the C was always right.
- Six observable triggers, the harmless-but-wrong `0xE4` case, exhaustive
  `0x00..0xFF` lead sweep, valid-UTF-8 widths, and invalid-UTF-8 span tiling all
  pass as **span-level** regressions.

## NOT frozen — two open items

1. **3 residual C-vs-Rust mismatches** appear only on an EXPANDED corpus
   (8921 docs, corpus=300/random=4000). They were invisible at corpus=200/
   random=2000, so the earlier "0 mismatches" was true only of the smaller set.
   Samples: `#3636`, `#4101`, `#4530` from the seeded fuzz corpus. Not yet
   diagnosed.
2. **The independent canonical reference disagrees with the oracle on 1525 /
   8716** arbitrary-byte documents (e.g. `b'hello ! world!'`). Since C and Rust
   agree on 8918/8921, the outlier is most likely my Python reference, not the
   oracle — but that is an assumption, not a finding, and the reference must be
   corrected or the comparison dropped before any hash is meaningful.

## Next

Diagnose the 3 residual mismatches; adjudicate the canonical reference against a
second independent implementation (HF `tokenizers` fed the byte-mapped string,
which is the only byte-correct way to drive it). Freeze the corrected
arbitrary-byte hash only when Rust == C across old corpus, all regressions,
minimal reproducers, arbitrary-byte corpus and fuzz, with the reference agreed.

---

# Residuals localised: C is ALSO defective (two of three)

Minimised from the 8921-doc corpus (batched delta-minimisation):
`#3636 -> bd9b9ae9`, `#4101 -> 898881ab`, `#4530 -> b12773` (3-4 bytes each).
All three reduce to **lone continuation bytes** — but they are NOT one defect
class, and the C is not the clean reference.

Adjudicated against an independent `regex` + reference-`encoder.py` transcription,
recording the GPT-2 byte map explicitly (bytes -> `bytes_to_unicode` char) so
all implementations are compared on the same string:

| input | byte-map chars | ref spans | ref ids | Rust | C |
|---|---|---|---|---|---|
| `bd9b9ae9` | `½`(No) `Ľļé`(Ll) | `[bd] [9b9ae9]` | `[121,249,21253]` | `[121,249,248,165]` **WRONG** | `[121,249,21253]` ok |
| `898881ab` | `īĪģ`(Ll) `«`(Pi) | `[898881] [ab]` | `[231,230,223,104]` | ok | `[231,230,43769]` **WRONG** |
| `b12773` | `±`(Sm) `'`(Po) `s`(Ll) | `[b1 27] [73]` | `[109,6,82]` | ok | `[109,338]` **WRONG** |

## C root cause (one class, two symptoms)

A byte-level alphabet has no "invalid UTF-8" class. Every one of the 256 bytes
maps to a real character with a real Unicode class — including bytes `0x80..0xBF`,
which map into `U+0100..U+0143` and are **letters**. The C does it wrong twice:

1. `gt_decode_next` treats a byte in `0x80..0xBF` in lead position as
   `GT_CLS_INVALID_UTF8`, i.e. "other". But e.g. `0x81 -> U+0123 'ģ'` is `Ll`, a
   **letter**. So `898881ab` should split `[898881]` (letters) from `[ab]`
   (`'«'` = `Pi`, other); the C made one pretoken and merged `91,171`.
2. The pretokenizer treats `GT_CLS_INVALID_UTF8` and "other" as **different
   classes that break a run**. In the GPT-2 pattern they are the *same* class
   (`[^\s\p{L}\p{N}]+`). So `b1 27` (`'±'` other + `'` other) must be one
   pretoken; the C stopped at the invalid byte, then re-entered at the
   apostrophe and matched the `'s` **contraction** branch — which the reference
   never reaches, because the Other run had already consumed the apostrophe.

Correct C design: map byte -> mapped codepoint FIRST (that is the alphabet),
then classify the codepoint. There is no decode step and no invalid class on the
bytes path. This also removes the pretokenizer's invalid/other distinction.

## Rust residual (case 1 only)

Rust emits `[bd9b9a] [e9]` where the classes are `No` then `Ll Ll Ll`, i.e. it
failed to break the number->letter run boundary. Separate defect from the C's;
not yet localised. Rust is correct on cases 2 and 3.

## Status

Phase 7 NOT frozen. `e749f8f` stands as the accepted candidate correction for the
lead-width bug; it is not sufficient for arbitrary-byte parity.

Next: fix the C's byte-map/class model, then localise the Rust case-1 boundary
defect, then re-run all four implementations before any hash is frozen.

---

# C migration: contract implemented, independent validation gate NOT met

`src/tools/gen_byteclass_table.py` generates `include/gt_byte_class.h` from the
single auditable derivation (byte -> GPT-2 byte-alphabet codepoint -> Unicode
category). No hand-maintained 256-entry table. ASCII is deliberately excluded:
0x20 is whitespace as a BYTE even though its byte-alphabet symbol U+0120 is a
letter.

`gt_pretok.c` now implements the frozen scanner contract (00..7F ASCII;
80..BF never a lead, width 1, byte-alphabet class; C0..F4 structural 2/3/4 with
failed continuation -> 1 byte / Other; F5..FF width 1).

State: selftest 337/337. Differential vs corrected Rust: **54/4821**.

## Residual attribution: consistent with the upstream defect, not proven

All 54 involve the high-byte region; **0 are pure ASCII**, so the ASCII and
whitespace paths are intact. But every one of the 54 also contains a
lead-shaped byte, so this test cannot isolate 80..BF as the sole cause. The
attribution to the documented upstream `80..BF` defect is PLAUSIBLE, NOT
ESTABLISHED.

## Gate status: NOT met

Required next, in order:
1. Repair the experimental Python reference's domain semantics (it regexes the
   byte-mapped string, where 0x20 has become U+0120 and is therefore a letter --
   it mis-classifies whitespace today). Then adjudicate C against it and
   against HF `tokenizers` on the explicitly byte-mapped domain.
2. Only an authority that is correct on 80..BF can validate C here. Corrected
   Rust currently is not: `e749f8f` still lets 80..BF reach the width-3 arm,
   which the sweep proved makes all 64 bytes able to consume 2-3 bytes. That is
   an upstream defect, recorded with reproducers `89 88 81` -> U+9201 and
   `BD 9B 9A` -> U+D6DA. Fixing it is optional for C's sake and must not become
   the critical path.
3. Re-verify `c8fa1aba...`, ASan/UBSan, and the exhaustive scanner properties
   (all 64 bytes of 80..BF width==1 under every bait) once the table is in.

Nothing frozen.
