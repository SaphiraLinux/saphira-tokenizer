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
