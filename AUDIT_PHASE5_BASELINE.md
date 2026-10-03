# Gigatoken Audit — Phase 5: Untouched Baseline

**Date:** 2026-10-03
**Baseline commit:** `fac0114b37120ec8a76362e9ee8e1c742aaafaef` ("Merge pull request #46 from marcelroed/cache-bound-minimal", 2026-08-05)
**Baseline worktree:** `/home/smalley/src-provenance/baseline-fac0114` (detached HEAD, full 368-commit history, created via `git worktree add`)
**Preserved evidence tree:** `/home/smalley/.local/src/saphira-gigatoken/gigatoken` — untouched, digest `b6c329cb…` unchanged
**Toolchain:** rustc 1.101.0-nightly, commit `0abfedbc7cd4e725f126913880c95800394f7c37`, dated 2026-10-02, LLVM 23.1.1 · cargo `f3865b2a…`
**Machine:** homer · i9-13900K / 24C32T / AVX2 (no AVX-512) · governor `powersave` · nproc 32 · MemAvailable 12.5–22 GiB (other workloads never disturbed)

---

## 0. Headline

**One CRITICAL product defect reproduced: non-UTF-8 bytes passed to a SentencePiece-backed tokenizer cause SIGSEGV / SIGBUS in the shipping release build.**

Everything else is green. The exact-token-ID correctness gate passes: **1416 Python tests passed, 0 failed**, across 8 tokenizer families plus a 20 MB adversarially-diverse corpus, in both debug and release builds. The Rust suite is green except for 17 tests that hard-require the original developer's private `~/data/` corpus.

---

## 1. Environment actually used

Per instruction, the resolved environment was **recorded, not pinned**:

```
rustc 1.101.0-nightly (0abfedbc7 2026-10-02)
  commit-hash : 0abfedbc7cd4e725f126913880c95800394f7c37
  commit-date : 2026-10-02
  host        : x86_64-unknown-linux-gnu
  LLVM version: 23.1.1
cargo 1.101.0-nightly (f3865b2a4 2026-09-29)
  commit-hash : f3865b2a4d1acc5276f6b3c67d0e057f4dab3928
rustup 1.29.1 (user-space, --no-modify-path); /usr/bin/cargo 1.98.1 intact
Python 3.13.15 audit venv (repo .python-version pin)
CARGO_BUILD_JOBS = 2–3 throughout (conservative; MemAvailable never exhausted)
```

---

## 2. Result classification

### PASS

| # | Surface | Command | Result |
|---|---|---|---|
| 1 | Library check | `cargo check --lib --locked` | **exit 0**, 24 s |
| 2 | Build: every target | `cargo test --all-targets --locked --no-run` | **exit 0** — all 9 benches + lib + bin compile |
| 3 | Dormant bench compiles | `cargo bench --no-run --bench unicode` | **exit 0** |
| 4 | Hand-written fuzz/differential | 8 tests, `--nocapture` | **8 passed, 0 failed, 0.86 s** |
| 5 | Rust unit tests (non-fixture) | `cargo test --lib` | **84 passed, 0 real failures** |
| 6 | Doc-tests | `cargo test --locked` | **ok, 0 tests** |
| 7 | Python collection | `pytest --collect-only` | **1449 collected**, exit 0 |
| 8 | **Exact-ID parity gate** | `tests/tokenizers/test_hf_parity.py` | **1056 passed, 8 skipped, 0 failed** |
| 9 | Python remainder | `pytest tests/ --ignore=tokenizers --ignore=dclm` | **353 passed, 8 skipped, 0 failed** |
| 10 | Diverse-corpus parity | `tests/test_encode_dclm.py` | **7 passed, 0 failed** |
| 11 | **Parity gate on RELEASE `.so`** | parity + dclm vs release build | **1063 passed, 8 skipped, 0 failed** |
| 12 | Packaging: wheel | `maturin build --profile dev --locked --compatibility pypi` | **exit 0** |
| 13 | Packaging: sdist | `maturin sdist` | **exit 0** |
| 14 | Clean-env wheel install + import | fresh venv, `pip install <wheel>` | **PASS**, version 0.10.0 |
| 15 | Repository CI smoke assertion | `b"".join(pretokenizer(b"Hello, world!")) == b"Hello, world!"` | **PASS** |
| 16 | `twine check --strict` | wheel **and** sdist | **PASSED** both |

### FAIL — real product defect

**P5-001 — CRITICAL — non-UTF-8 bytes → SentencePiece-backed tokenizer → SIGSEGV / SIGBUS.**
Full record: Unified task `d6cd097d-0d72-4c8e-9107-bc182b58fecb`.

Minimal reproducer (outside the source tree), on the **release** build:

```python
from gigatoken.gigatoken_rs import SentencePieceTokenizer, hub_file
SentencePieceTokenizer.from_hf(
    hub_file("TinyLlama/TinyLlama-1.1B-Chat-v1.0", "tokenizer.json")
).encode(b"\xff")          # -> SIGSEGV (139)
```

| Entry point | debug build | **release build (ships)** |
|---|---|---|
| `encode(b"\xff")` | catchable `PanicException` | **SIGSEGV (139)** |
| `encode_batch([b"\xff"])` | catchable `PanicException` | **SIGSEGV (139)** |
| `encode_files(TextFileSource(..., separator=None))` | SIGABRT + core dump | **SIGBUS (135)** |
| `encode_files(..., separator=b"\n")` | SIGABRT + core dump | **SIGBUS (135)** |

Payload matrix (TinyLlama, release): `\xff` SIGSEGV · `a\xff` SIGSEGV · `\x80` SIGSEGV ·
`\xf0\x9f` SIGSEGV · `\xc3\x28` clean · `\xed\xa0\x80` clean · `hello` clean.
**A single byte `0xff` suffices.**

Affected: SentencePiece-backed only — confirmed SIGSEGV on
`TinyLlama/TinyLlama-1.1B-Chat-v1.0` and `microsoft/Phi-3-mini-4k-instruct`.
Unaffected: byte-level BPE — `openai-community/gpt2`, `Qwen/Qwen2-1.5B-Instruct`,
`allenai/Olmo-3-1025-7B`, `deepseek-ai/DeepSeek-V3` all clean with invalid bytes.

Root cause located: the SentencePiece path converts `&[u8]` → `&str` with
`from_utf8_unchecked` and then genuinely relies on valid UTF-8 downstream
(`src/lib.rs:301`, `src/lib.rs:426`, `src/batch.rs:1004/1025/1049`,
`src/bpe/sentencepiece.rs:1429`). The only guards are `debug_assert!`
(`src/lib.rs:295`, `src/lib.rs:423`) which are **compiled out in release**.
In debug, std's optional UB check (`unreachable_unchecked` in
`core::str::validations`) aborts; in release there is no check and the UB
manifests as SIGSEGV/SIGBUS.

Why this is a contract defect rather than caller error: the shipped type stub
advertises `encode(self, input: str | bytes)`,
`encode_batch(self, inputs: list[str] | list[bytes] | ...)` and
`encode_files(...)` with **no UTF-8 precondition** in the stub, the README, or
`gigatoken/_tokenizer.py:202`. The API invites arbitrary bytes and answers with a
memory-safety violation rather than a clean exception. The repository already
contains the correct pattern for the *separator* (`src/lib.rs:314-321` returns
`PyValueError` for a non-UTF-8 separator) — the document bytes were simply never
given the same treatment.

**Not repaired.** No source edits were made.

### FAIL — missing external fixture (17 Rust tests)

All 17 failures are absent-fixture, **zero product defects**. `cargo test --lib`
→ `84 passed; 17 failed; 20 ignored`. `src/main.rs` re-runs the same suite
(`80 passed; 17 failed; 18 ignored`).

| Fixture required (hardcoded, no env override, no skip guard) | Tests |
|---|---|
| `~/data/owt_train.txt` (the full ~11.9 GB OWT file) | 12 |
| `~/data/tokenizers/r50k_base.tiktoken` | 2 |
| `~/data/TinyStoriesV2-GPT4-valid.txt` | 3 |

Every path is built as `std::env::home_dir().unwrap().join("data")` — e.g.
`src/pretokenize/mod.rs:706`, `src/bpe/tiktoken.rs:2487`,
`src/pretokenize/reference/simd.rs:806`, `src/pretokenize/reference/combinator.rs:511`.

**This is a real reproducibility defect**, recorded as such rather than papered
over by manufacturing a file. Two distinct sub-observations:

- The suite is **not hermetic**: a fresh clone on any machine other than the
  author's fails 17 tests with no skip path.
- The author marks the *large* corpus tests `#[ignore]` (18–20 of them, with
  reasons like `"reads 1 GB of OWT; run explicitly in release mode"`) but leaves
  the *100 MB* differential tests un-ignored, so they hard-fail. The
  `#[ignore]` policy is inconsistent between the 100 MB and 1 GB tiers.

Python-side skips are all legitimate and correctly guarded: 8 × OWT-absent,
2 × torch-absent (optional), 3 × `Tokenizer` API marked
`"High-level Tokenizer API not yet implemented"`, 2 × local corpus absent.

### FAIL — dormant historical target

**B6 resolved empirically — and the answer differs from the hypothesis.**

`benches/unicode.rs` **compiles** (`cargo bench --no-run --bench unicode` → exit 0)
and **runs**, reporting:

```
Running benches/unicode.rs
0 tests, 0 benchmarks
```

So it is **not** a build failure. Because `autobenches` is never set, Cargo
auto-discovers it with `harness = true`; libtest therefore supplies `main`,
`criterion_main!`'s `main` is dead code, and the target is a **silent no-op**.
It builds, runs, and measures nothing.

Git history explains it: the `[[bench]]` entry was added in `eedebcc` (2025-10-09)
and **deliberately commented out** in `f76c34b` (2026-07-09), leaving
`# [[bench]] / # name = "unicode" / # harness = false`. **Intentionally dormant,
author-maintained. Not repaired** (and see §5: upstream deletes the file outright
on `cad83b0`).

### NOT RUN — resource-controlled deferral

| Item | Reason |
|---|---|
| 11.9 GB OWT throughput | machine contended; deferred to the controlled window |
| RSS / cache-growth stress | MemAvailable 12–22 GiB, other workloads resident |
| multi-GB mmap / sparse-file behaviour | same |
| full thread-scaling sweep (1/2/4/8/physical/SMT) | requires quiet machine |
| headline throughput vs README claims | **must not** be published from here |
| `~1000×` claim reproduction | out of scope on this hardware by instruction |
| `twine check` on release-script artefacts | release script requires macOS + Docker |

### COVERAGE LIMITATION

| Item | Status |
|---|---|
| **AVX-512** | **Untested.** i9-13900K (Raptor Lake) does not expose AVX-512. No BIOS/microcode/spoofing attempted. Upstream's own `x86_port_plan.md:307` already records that tier as "never been timed on metal". Separate-host item. |
| **Scalar SIMD fallback** | Not exercised. `simd_scanner_available()` is true here (AVX2), so the scalar arm was never selected. Would need a pre-AVX2 host or a build-time override (not done — no source edits). |
| **aarch64 / NEON** | Not exercised. No ARM host available. |
| **musl / glibc variants, macOS, Windows** | Not exercised. Only `x86_64-unknown-linux-gnu` glibc. |
| **SIMD tier actually exercised** | **AVX2** (`avx2_scanner_available()` true, `avx512_scanner_available()` false), with `crc_hash_selected()` true via SSE4.2. Runtime dispatch confirmed correct: it selected AVX2 and did not attempt AVX-512. |
| **SIMD ≡ scalar equivalence** | Verified on AVX2 by the 8 hand-written differential tests, all passing. |

---

## 3. Correctness gate detail

**Exact token IDs — not decoded text — are the gate.** All comparisons assert ID
lists for equality.

| Suite | Assertions | Result |
|---|---|---|
| `test_hf_parity.py` — 39 texts × 8 tokenizers | 312 | pass |
| `test_hf_parity.py` — 31 special/added-token probes × 8 tokenizers | 248 | pass |
| `test_hf_parity.py` — EOT id, decode roundtrip, batch≡serial × 8 | 24 | pass |
| `test_hf_parity.py` subtotal | **1056** (8 skips = OWT absent) | **pass** |
| `test_encode_dclm.py` — exact-ID parity over 20 MB diverse corpus, tinyllama (SP) + olmo3 (byte-BPE) + deepseek_v3 (byte-BPE + NFC) | 7 | pass |
| Remainder (`test_sentencepiece`, `test_hf_compat`, `test_from_tiktoken`, `test_tiktoken_compat`, `test_from_hf`, `test_encode_files`, `test_multiprocessing`, `test_parquet_source`, `test_file_source`, `test_config_dispatch`, `test_bpe_train_compare`, `test_cache_budget`, `test_encode`, `test_load_hf`) | 353 | pass |

Tokenizer families covered by exact-ID parity: `gpt2`, `olmo3`, `qwen2`,
`qwen3_5`, `modernbert`, `glm5_2`, `deepseek_v3`, `deepseek_v4`, plus
TinyLlama, Phi-3, Llama-legacy (SentencePiece) and r50k/cl100k/o200k (tiktoken).

**Crash-free is not the claim being made.** The claim is exact token-ID equality,
and it held on every case that did not abort.

### SIMD vs scalar differential (hand-written, as shipped)

```
pretokenize::fast::r50k::tests::mask_iter_matches_shipped_edge_cases ... ok
pretokenize::fast::r50k::tests::mask_iter_matches_shipped_fuzz ... ok
pretokenize::fast::cl100k_family::tests::family_mask_matches_scalar_padded_cases ... ok
pretokenize::fast::cl100k_family::tests::family_mask_matches_scalar_fuzz ... ok
pretokenize::fast::cl100k_family::tests::family_straddling_digit_char_phase ... ok
pretokenize::fast::o200k_family::tests::o200k_family_mask_matches_scalar_padded_cases ... ok
pretokenize::fast::o200k_family::tests::o200k_family_mask_matches_scalar_fuzz ... ok
bpe::tiktoken::walker_edge::walker_boundary_fuzz_memoized_vs_reference ... ok
test result: ok. 8 passed; 0 failed; 0 ignored; 0 measured; 113 filtered out; finished in 0.86s
```

No `proptest`, `cargo-fuzz`, Miri or Loom was added, per instruction. The existing
hand-written fuzzers were run as shipped.

---

## 4. Additional observations (not defects)

**O1 — the entire library is compiled twice.** `src/main.rs` declares
`mod bpe; mod bpe_train; mod input; mod load_tokenizer; mod pretokenize;
mod test_hub; mod token;`, recompiling the whole crate into the bin target
instead of depending on the lib. `mod batch` and `mod bindings` are absent from
the bin, which exactly explains the test-count asymmetry (lib 84 passed/20 ignored
vs bin 80 passed/18 ignored; `batch.rs` carries the 2 ignored tests).
Consequence: `cargo test` runs the suite twice (~113 s + ~107 s) and a bare test
name is ambiguous about which target produced it.

**O2 — wheel platform tag.** A local build yields
`manylinux_2_34_x86_64`, not the `manylinux_2_17` / `manylinux2014_x86_64` the
release script produces via Docker. Expected locally, not a defect.

**O3 — maturin version skew.** The audit used maturin 1.15.0;
`scripts/build_release_cross_platform.py:38` hard-pins `1.14.1`. `pyproject.toml`
allows `>=1.14.1,<2.0`, so packaging results here were produced by a newer
maturin than the release path will use. Recorded so packaging results are read
correctly.

**O4 — patchelf absent.** `maturin develop` warned it could not set rpath.
Harmless for an editable local install; noted.

---

## 5. Independent inspection of `cad83b0` (`simplify-refactor`)

Inspected only. **Not merged, not cherry-picked, not built, not mixed into the
baseline.** Baseline remained `fac0114` throughout.

| Property | Value |
|---|---|
| Commit | `cad83b039e3ecdff811bef6c6fd3ce62a8f2afaa` |
| Date | 2026-09-01 15:09:36 -0700 |
| Subject | "Simplify the codebase: remove dead paths, dedupe, trim comments" |
| Author | Marcel Rød |
| Merged into `upstream/main`? | **NO — unmerged** |
| Merge-base with `upstream/main` | `fac0114b37120ec8a76362e9ee8e1c742aaafaef` (exactly our baseline) |
| Ahead / behind | **1 / 0** — the only branch ahead of main |
| Diff size | **54 files, +3366 / −11976** |

### What invariant it changes

Predominantly **comment trimming**, plus two large deletions:

- **Deletes the entire `src/pretokenize/reference/` oracle module** — 5 files,
  2339 lines: `state_machine.rs`, `combinator.rs`, `simd.rs`, `avx512.rs`,
  `mod.rs`. `benches/pretokenize.rs` loses 46 of 47 lines (the shootout against
  those oracles).
- **Deletes the bin target** (`src/main.rs`, 99 lines; `Cargo.toml` −12). This
  would incidentally fix observation **O1** (the double compilation).
- Also deletes `src/pretokenize/pretokenize_traits.rs` (49),
  `benches/pretokenize_profile.rs`, `benches/simdutf_transcode.rs`,
  `benches/unicode.rs`, `tests/bench_file_source.py`, `tests/bench_train_encode.py`.

### Cache-budget semantics

**Unchanged — comment trimming only.** Surviving symbol counts,
main → `cad83b0`: `DEFAULT_MAX_CACHE_BYTES` 9→8, `set_max_cache_bytes` 25→22,
`wipe_if_over_budget` 2→2, `CacheBudget` 10→10, `get_max_cache_bytes` 2→2. The
diff on `src/bpe/tiktoken.rs` for those symbols is entirely doc-comment text
(e.g. "Configured total budget in bytes; forks inherit it verbatim" →
"Token-arena sub-budget in entries"). `512 MiB per encode worker`, per-worker
full-budget semantics, and generational wipe all persist verbatim.

### Correctness implications — and it does NOT fix P5-001

`from_utf8_unchecked` survives on the branch (12 sites → 5, reduced only because
`main.rs` and `reference/*` are deleted):

```
cad83b0:src/batch.rs:749, :764, :784
cad83b0:src/bpe/sentencepiece.rs:1351
cad83b0:src/lib.rs:308
```

And the guards are **still debug-only** on `cad83b0`:

```
cad83b0:src/lib.rs:215   debug_assert!(regions.iter().all(|d| std::str::from_utf8(d).is_ok()));
cad83b0:src/lib.rs:306   debug_assert!(std::str::from_utf8(b).is_ok());
```

**Conclusion: the reproduced CRITICAL soundness defect survives this refactor
unchanged.** No assumption that newer is better — verified, not assumed.

Second correctness concern, opposite in sign: deleting `reference/` removes the
independent differential oracles (`state_machine`, `combinator`, `simd`,
`avx512`) that the audit brief explicitly asks to be used, and removes the
in-module differential tests that live beside them.

### Performance implications

**Cannot be assessed without measurement, and no measurement was taken** (branch
not built; instruction forbids mixing it into the baseline). Structural risk to
note for later: the branch rewrites every scheme module (`r50k.rs` −682,
`cl100k_family.rs` −612, `mask.rs` −524, `mod.rs` +735, `qwen2.rs` −394) — i.e. it
touches exactly the SIMD hot paths whose current shape carries measured
invariants documented in `pretokenizer_optimization_log.md` and
`x86_port_plan.md`. Per the standing rule that upstream's negative results are
preserved and not re-litigated without new evidence, any adoption of this branch
needs the full interleaved A/B protocol from `x86_port_plan.md:238-245`.

### Is it intended for merge?

**Cannot be determined from Git.** It is unmerged, has no PR reference in the
fetched refs, and is 1 commit ahead of `main`. The sibling branch
`codex/manageability-simplifications` (`f613a50`, 2026-07-15, "Restore decision-
critical rationale") is **2 ahead / 91 behind** `main` and is badly stale; its tip
message suggests an earlier simplification pass was partly reverted. Both point to
an active, unfinished maintainability effort rather than a landed direction.

---

## 6. Integrity at completion

| Check | Result |
|---|---|
| `git rev-parse HEAD` | `fac0114b37120ec8a76362e9ee8e1c742aaafaef` |
| HEAD == baseline | **YES** |
| `git status --porcelain` | **0 entries** |
| Tracked changes | **0** |
| `git diff fac0114 --stat` | **0 lines** |
| Ignored artefacts only | 7 (`target/`, `dist/`, `gigatoken/gigatoken_rs.abi3.so`, 2× `__pycache__/`) |
| **Mystery tree digest** | `b6c329cb82f791dbda2a50c7b55d016911622d718ffd73554d21d9cea5eb757a` — **unchanged from Phase 1** |
| Governor | `powersave` — **unchanged** |
| Other workloads' memory | **never reclaimed, never disturbed** |

**No source edits were made at any point in Phase 5.** Build/cache artefacts left
in place deliberately.

---

## 7. Performance smoke — NOT A PERFORMANCE RESULT

```
LABEL: CONTENDED / POWERSAVE / NOT A PERFORMANCE RESULT
cpu: 13th Gen Intel(R) Core(TM) i9-13900K, 24 cores
gigatoken:    0.495 s |     110.00 MB at   222.34 MB/s |    31.92 Mtok at   64.52 Mtok/s
```

Conditions at run time: governor `powersave`, loadavg 8.26 on 32 threads,
MemAvailable 20.6 GiB with other workloads resident. Input was a real 110 MB /
16,034-line prefix of the DCLM shard (not OpenWebText).

This figure exists **solely to prove the benchmark machinery operates**. It is
deliberately **not** compared against the README headline or any upstream table,
and no speedup claim is derived from it. The README's own numbers range from
2.51 GB/s to 24.53 GB/s depending on tokenizer and hardware; quoting 222 MB/s
beside them without a controlled machine would be exactly the error this audit
exists to avoid.

---

## 8. Evidence index

| Artefact | Path |
|---|---|
| Baseline worktree (`fac0114`, clean) | `/home/smalley/src-provenance/baseline-fac0114` |
| Combined repo (both histories) | `/home/smalley/src-provenance/akadata` |
| Mystery-tree manifest + digest | `/home/smalley/src-provenance/audit/manifest-tree.tsv`, `tree-digest.txt` |
| Command logs 01–15 | `/home/smalley/src-provenance/audit/logs/` |
| **P5-001 minimal reproducer** | `/home/smalley/src-provenance/repro/scope.py` |
| P5-001 per-case matrix | `/home/smalley/src-provenance/repro/case.py` |
| P5-001 full reachability probe | `/home/smalley/src-provenance/repro/utf8_unchecked_reach.py` |
| Audit Python venv (3.13.15) | `/home/smalley/src-provenance/audit-venv` |
| Clean packaging venv | `/home/smalley/src-provenance/clean-venv` |
| 110 MB perf-smoke input | `/home/smalley/src-provenance/perfdata/owt_like_100mb.jsonl` |

---

## 9. Recommended next steps (for approval — nothing actioned)

1. **P5-001 first.** Smallest repair: replace the release-compiled-out
   `debug_assert!` with a real runtime check returning `PyValueError`, mirroring
   the existing separator guard at `src/lib.rs:314-321`, applied in
   `sp_encode_docs_ragged` and `sp_encode_files_docs(_serial)` where no guard
   exists today. Regression test: `b"\xff"` via `encode`, `encode_batch`,
   `encode_files` must raise cleanly, not signal; byte-level-BPE invalid-bytes
   behaviour must remain unchanged.
2. **Test-fixture hermeticity.** Introduce an env-var override for the corpus
   directory and convert the 17 hard panics into skips, matching the `#[ignore]`
   intent already expressed for the large tiers.
3. **Dated toolchain pin** (`nightly-YYYY-MM-DD`) as its own evidence-backed
   task, proving equivalence against this baseline first — **not** done now.
4. **`cad83b0` carry-forward decision** — deliberately deferred. Its deletion of
   `reference/` would remove the differential oracles this audit depends on, and
   it does **not** fix P5-001.
5. Everything in §2 "NOT RUN" waits for a quiet machine and a governor change
   under Andrew's control.

**Nothing repaired. Nothing optimised. Stopping for review.**