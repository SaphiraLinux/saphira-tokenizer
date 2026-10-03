# saphira-tokenizer

Tokenizer semantics and exact token-ID compatibility.

    tokenizer semantics
    GPT-2 / r50k, Qwen2, SentencePiece, etc.
    Rust correctness implementation
    independent C scalar oracle
    later SIMD
    later CUDA only if earned
    exact token-ID compatibility

The name says what it is, without inheriting any one implementation's identity.
`saphira-llm` consumes this as a component later; it does not swallow a project
called something else whose name no longer describes the work.

## Layout

    src/                      independent scalar C implementation (the oracle)
      include/                stage headers: one small surface per stage
      lib/                    one translation unit per pipeline stage
      tools/                  gt_dump_ids (differential CLI), gen_unicode_tables.py
      tests/                  gt_selftest
      data/ucd/               Unicode 16.0 source data (independent of Rust)
    AUDIT_PHASE1-4_*.md      provenance and environment audit
    AUDIT_PHASE5_*.md         baseline and defect record
    PHASE7_FINDINGS.md        scalar C status and the open oracle defect

## Pipeline stages

Each stage is a separate translation unit with a small interface, so a
disagreement with another implementation localises to a stage rather than to
"the tokenizer":

    input -> special -> pretok -> bytemap -> bpe -> vocab -> ids

`gt_json` loads tokenizer.json; `gt_tokenizer` wires the stages; `gt_special`
holds added-token policy. Inputs are `(pointer, length)` everywhere: the bytes
path accepts arbitrary bytes, including embedded NUL, invalid UTF-8 and lone
continuation bytes. Nothing validates UTF-8 before the pretokenizer has decided
how to treat a byte.

## Building

    make            # -O0, full warning set, plain scalar C11
    make asan       # same code under AddressSanitizer + UBSan
    make tables     # regenerate Unicode tables from data/ucd/

## Status

`make && ./build/gt_selftest tests/fixtures/gpt2_tokenizer.json` — 337 checks,
0 failures, clean under ASan+UBSan.

Differential vs the Rust oracle: **155 disagreements / 4821 documents**, all
inside the arbitrary-bytes fuzz set. This is a known defect **in the Rust
oracle**, not in the C — see PHASE7_FINDINGS.md for the frozen evidence, the
minimal reproducer, the stage localisation, and the narrowed suspect. Phase 7
is not frozen until that is resolved and corrected Rust == C scalar.
