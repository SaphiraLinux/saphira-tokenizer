# Gigatoken Audit — Phase 1–4: Provenance and Environment

**Date:** 2026-10-03
**Auditor:** automated audit session
**Status:** Phase 1–4 complete. **Phase 5 (untouched baseline) NOT started** — held for review.
**Source of record:** `/home/smalley/.local/src/saphira-gigatoken/gigatoken` (byte-preserved, never modified)
**Provenance/evidence area:** `/home/smalley/src-provenance/` (clones, manifests, audit venv)

---

## 0. Headline result

The untracked source tree at `gigatoken/` is **byte-for-byte identical to
`upstream/main` @ `fac0114b37120ec8a76362e9ee8e1c742aaafaef`** — proven twice, by two
independent instruments, and the match is **unique** among all 20 fetched refs.

It is **not** the AKADATA snapshot. AKADATA's `main` is a strict ancestor, 4 commits
behind, and contains **zero** AKADATA-local commits.

Three of the audit's starting premises were wrong. See §7.

---

## 1. Phase 1 — Byte-exact snapshot manifest

The tree was **not modified**. Manifest generated read-only; digest independently
regenerated and confirmed identical.

| Property | Value |
|---|---|
| Regular files | **173** |
| Symlinks | **0** |
| Empty directories | **0** |
| Special files (fifo/socket/device) | **0** |
| Total regular-file bytes | **4,225,732** |
| Mode histogram | 168 × `644`, 5 × `755` |
| Largest file | 1,355,256 bytes (`uv.lock`) |
| Build artefacts present at capture | **none** (no `target/`, no `.so`, no `__pycache__`) |

**Tree digest** (SHA-256 over the canonicalised manifest body):

```
b6c329cb82f791dbda2a50c7b55d016911622d718ffd73554d21d9cea5eb757a
```

Manifest records, per entry: record kind, NUL-safe relative path, SHA-256, octal
mode, size — for regular files; verbatim link target for symlinks; `EMPTY` for
empty directories. Sorted with `LC_ALL=C` for determinism.

**Executable-bit files (5):**
`profiling/analyze.sh`, `profiling/profile.sh`, `profiling/zen5/repro.sh`,
`scripts/build_release_cross_platform.py`, `scripts/profile-cpu.fish`

**Outer repository state at capture:** branch `Master`, **zero commits**, **no
remotes**, **zero tags**, **zero refs**, **zero objects**, empty reflog,
`.git/config` containing only `[core]`. `git status` reported `MCP.md`,
`PROCESS.md`, `gigatoken/` as untracked. Nothing was cleaned or reset.

**Retraction of an earlier claim of mine:** I previously asserted "no gigatoken
clone exists anywhere on this host", based on `find /home/smalley -maxdepth 5`.
That search was too shallow — `saphira-gigatoken/gigatoken` is deeper than the limit
and its parent does not itself match `gigatoken*`. **The uniqueness claim was not
proven by that command.** Only the narrower fact is established: no *other* clone
was found within the searched depth. This did not affect provenance recovery.

---

## 2. Phase 2 — Real Git provenance

Both histories were brought into **one object database** (the AKADATA clone, with
upstream added as a second remote) so that `merge-base` and `--left-right
--cherry-pick` are meaningful. Full history, no `--depth`. Default branches were
discovered with `git ls-remote --symref` rather than assumed — **both are `main`**.

| Fact | Value |
|---|---|
| Root commit (both repos) | `654ac1fcbf828d1650a150796f9fbe6d22ea47e7` — 2025-10-04 "Initial commit" |
| **AKADATA `main` HEAD** | `34a1599f0c0ae7d7cd0d1c530e6522320158b360` |
| AKADATA HEAD date | 2026-07-25 12:15:53 -0700, "Bump minor version to 0.10.0" |
| AKADATA commit count | 364 |
| **Upstream `main` HEAD** | `fac0114b37120ec8a76362e9ee8e1c742aaafaef` |
| Upstream HEAD date | 2026-08-05 17:11:21 -0700, "Merge pull request #46 from marcelroed/cache-bound-minimal" |
| Upstream commit count | 368 |
| **Merge-base** | `34a1599f0c0ae7d7cd0d1c530e6522320158b360` — **identical to AKADATA HEAD** |
| Ahead / behind | **AKADATA 0 ahead, 4 behind** |
| `--cherry-pick` unique | **AKADATA 0, upstream 4** (no patch-equivalent commits) |
| `akadata/main` is ancestor of `upstream/main` | **YES** |
| `upstream/main` is ancestor of `akadata/main` | NO |
| Tags in either repo | **none** |

### Relationship classification

**AKADATA `akadata/gigatoken` is a pure, unmodified mirror-fork of
`marcelroed/gigatoken`, frozen at upstream commit `34a1599`.** It carries:

- **zero** AKADATA-authored commits
- **zero** patch-equivalent divergence
- **zero** tags of its own

It is an *exact historical upstream snapshot*, not "snapshot + AKADATA changes" and
not a "diverged development branch". The `34a1599` observation seen externally was
**correct and current** — AKADATA has not moved.

### The 4 upstream-only commits (all cache-bound work)

| SHA | Date | Subject | Correctness | Mem-safety | Perf | Evaluate later? |
|---|---|---|---|---|---|---|
| `9e9271b08b71a8552a32b967065ae47490782b5b` | 2026-07-26 14:07:29 -0700 | Bound the encode caches by default (512 MiB per worker, issue #36) | yes — adds budget derivation | yes — bounds memory growth | yes | **already in our tree** |
| `7882dc88a7b2f35ee7c522f21981c143453117ce` | 2026-07-26 15:03:10 -0700 | Bound the SentencePiece encode caches under the same budget | yes | yes | yes | **already in our tree** |
| `aa58b2d` | 2026-08-05 | CJK note in README | no | no | no | **already in our tree** (docs only) |
| `fac0114b37120ec8a76362e9ee8e1c742aaafaef` | 2026-08-05 17:11:21 -0700 | Merge PR #46 from `cache-bound-minimal` | — | — | — | **already in our tree** |

Because all four are already present, there is **no post-snapshot upstream
correctness or memory-safety work left to carry forward**. The carry-forward phase
reduces to unmerged branches (§6).

### Exact origin of the cache-bound code

```
9e9271b08b71a8552a32b967065ae47490782b5b  2026-07-26 14:07:29 -0700
  "Bound the encode caches by default (512 MiB per worker, issue #36)"
```
introduced, in one commit: `DEFAULT_MAX_CACHE_BYTES` (in `src/bpe/tiktoken.rs`),
`src/bindings/cache.rs`, `tests/test_cache_budget.py`, `wipe_if_over_budget`,
`set_max_cache_bytes`. SentencePiece parity followed ~1 hour later in `7882dc8`.
Branch `cache-bound-minimal` tip `7882dc8` differs from our tree by exactly the
README CJK note — consistent with `aa58b2d` landing on `main` separately.

---

## 3. Relationship of the untracked tree — the decisive comparison

Two independent instruments, both applied with the *same* algorithm to the mystery
tree and to Git-derived reference data.

**Instrument 1 — SHA-256 content manifest (filesystem vs. per-commit worktree):**

| Commit | Files | Digest (16) | Verdict |
|---|---|---|---|
| `34a1599` (AKADATA) | 171 | `85712dc8583673de` | differs |
| `9e9271b` | 173 | `ab2d16e6746eac5f` | differs |
| `7882dc8` | 173 | `d4f5e1480bb16ffd` | differs |
| `aa58b2d` | 171 | `744b641289c5ff3d` | differs |
| **`fac0114` (upstream main)** | **173** | **`b6c329cb82f791db`** | **EXACT MATCH** |

**Instrument 2 — Git blob SHA-1 + mode (`git hash-object` vs `git ls-tree -r`):**
`refs/remotes/upstream/main` → **EXACT MATCH** on all 173 files (path + blob id +
mode). Scanning every ref under `refs/remotes` and `refs/tags`, **only
`refs/remotes/upstream/main` matches.**

### Comparison taxonomy (per the required precision)

| Category | Result |
|---|---|
| Tracked **content** exact | **YES** — all 173 files, all SHA-256 and all blob SHA-1 identical |
| Tracked **modes** exact | **YES** — 168 × `644`, 5 × `755`, identical on both sides |
| **Symlinks** exact | **YES** — zero on both sides |
| Filesystem-only **empty directories** | **NONE** — zero on both sides (Git cannot encode these, and none exist) |
| Filesystem-only **extra files** | **NONE** beyond gitignored build artefacts (§4) |
| **Missing** tracked files | **NONE** — no path in `upstream/main` is absent from the tree |

### Differences vs. the AKADATA snapshot (`34a1599`), for the record

20 differing lines. 2 paths exist only in our tree; 0 paths exist only in AKADATA;
9 paths have changed content:

- extra: `src/bindings/cache.rs`, `tests/test_cache_budget.py`
- modified: `README.md`, `gigatoken/__init__.py`,
  `gigatoken/gigatoken_rs/__init__.pyi`, `src/bindings/mod.rs`,
  `src/bpe/pretoken_cache.rs`, `src/bpe/sentencepiece.rs`, `src/bpe/tiktoken.rs`,
  `src/lib.rs`, `src/load_tokenizer/hf.rs`

This is exactly the cache-bound diff — confirming the mechanism by which the tree
diverges from AKADATA.

---

## 4. Build artefacts created during Phase 3 (disclosed)

`cargo check` created `gigatoken/target/` (407 MB). It is **gitignored**
(`.gitignore:103`), did not exist at Phase 1, and **altered no tracked file** —
the post-build manifest (excluding `target/`) re-digests to
`b6c329cb82f791dbda2a50c7b55d016911622d718ffd73554d21d9cea5eb757a`, identical to
the Phase 1 baseline. Not removed, per "do not clean".

The audit Python venv was deliberately created **outside** the preserved tree, at
`/home/smalley/src-provenance/audit-venv`, so it could not perturb the manifest.

---

## 5. Phase 3 — Environment

### Installation method (non-perturbing)

```
rustup-init 1.29.1 (d95a37b6a 2026-08-13), fetched from static.rust-lang.org
rustup-init --no-modify-path --default-toolchain none --profile minimal -y
```

- **Global shell environment UNMODIFIED.** No rc file (`~/.bashrc`,
  `~/.bash_profile`, `~/.profile`, `~/.zshrc`) references `cargo` or `rustup`,
  before or after. `~/.cargo/bin` is deliberately **not** on `PATH`; the audit uses
  an explicit per-invocation `PATH="$HOME/.cargo/bin:$PATH"`.
- **System toolchain intact:** `/usr/bin/cargo` `cargo 1.98.1 (797e8a9bc 2026-08-05)`
  (Arch package `rust 1:1.98.1-1`), `/usr/bin/rustc` `1.98.1 (48a229cea 2026-09-01)`.
  Nothing removed, upgraded, or shadowed globally.
- This host had no `~/.rustup` and no rustup binary before this session.

### Resolved nightly — exact identity

The repository selects it automatically:

```
$ rustup show active-toolchain
nightly-x86_64-unknown-linux-gnu (overridden by '.../gigatoken/rust-toolchain.toml')
```

| Field | Value |
|---|---|
| rustc release | **1.101.0-nightly** |
| rustc commit-hash | **`0abfedbc7cd4e725f126913880c95800394f7c37`** |
| rustc commit-date | **2026-10-02** |
| host | `x86_64-unknown-linux-gnu` |
| **LLVM version** | **23.1.1** |
| cargo release | 1.101.0-nightly |
| cargo commit-hash | `f3865b2a4d1acc5276f6b3c67d0e057f4dab3928` |
| cargo commit-date | 2026-09-29 |
| `rustup which rustc` | `/home/smalley/.rustup/toolchains/nightly-x86_64-unknown-linux-gnu/bin/rustc` |

**Reproducibility observation (not acted on):** `rust-toolchain.toml` pins only
`channel = "nightly"` — **no date**. The resolved compiler is dated **2026-10-02**,
i.e. the day before this audit. The floating pin is a genuine reproducibility
hazard. Per instruction it is **not pinned now**; the untouched baseline is
established first, and a dated-pin recommendation becomes a separate task with its
own evidence and regression run.

### Toolchain requirement — proven naturally, no `RUSTC_BOOTSTRAP`

`RUSTC_BOOTSTRAP` was **never set** in any probe, build, test, or baseline. Two
*independent* nightly requirements were demonstrated:

**Probe A — system stable 1.98.1, no overrides, repository as written:**

```
$ cargo check --lib --locked          # /usr/bin/cargo, no rustup on PATH
error: failed to parse manifest at '.../gigatoken/Cargo.toml'
Caused by:
  feature `profile-rustflags` is required
  The package requires the Cargo feature called `profile-rustflags`, but that
  feature is not stabilized in this version of Cargo (1.98.1).
```

**→ System stable rejects the repository outright, at manifest parse.**

**Probe A2 — `#![feature(portable_simd)]` on system stable** (standalone probe
file in `/tmp`, repository untouched):

```
error[E0554]: `#![feature]` may not be used on the stable release channel
```

**Probe A3 — same file on the repository-selected nightly:** compiles, links, runs,
prints `7`. Exit 0.

So nightly is required for **two** reasons: the `portable_simd` feature in
`src/lib.rs:1` / `src/main.rs:1`, and the `[profile.profiling] rustflags` key
(Cargo.toml:75) gated by `.cargo/config.toml`'s `[unstable] profile-rustflags`.

**Probe B — nightly, repository as written:** `cargo check --lib --locked`
**exit 0**, clean. (`dev` profile.)

**Probe C — `profile-rustflags` accepted:**
`cargo check --lib --profile profiling --locked` **exit 0** — the `[profiling]`
profile with `rustflags = ["-C", "force-frame-pointers=yes"]` builds. Negative
control: the same invocation under system stable reproduces the
`feature profile-rustflags is required` manifest error.

Registry after dependency download: **114 MB, 210 `.crate` files cached**
(registry was empty at session start).

### Python audit environment

Created **outside** the preserved tree: `/home/smalley/src-provenance/audit-venv`
on **CPython 3.13.15**, honouring the repository's `.python-version` pin of `3.13`
(the system interpreter is 3.14.7 and was not used).

| Package | Version |
|---|---|
| maturin | **1.15.0** |
| tokenizers | 0.23.2 |
| tiktoken | 0.14.0 |
| sentencepiece | 0.2.2 |
| transformers | 5.18.0 |
| numpy | 2.5.3 |
| awkward | 2.14.0 |
| polars | 1.44.2 |
| zstandard | 0.25.0 |
| pytest | 9.1.1 |
| ruff | 0.16.10 |
| ty | 0.0.84 |

`torch` deliberately **not** installed — `tests/test_hf_compat.py` gates it behind
`importorskip("torch")`, so it is optional for the parity work that matters.

**Observation for later:** `maturin 1.15.0` satisfies `pyproject.toml`'s
`maturin>=1.14.1,<2.0`, but `scripts/build_release_cross_platform.py:38` hard-pins
`MATURIN_VERSION = "1.14.1"`. The audit interpreter is therefore *newer* than the
release script's pin. Not a defect; recorded so Phase 5 packaging results are
interpreted against the right maturin.

---

## 6. B6 — `benches/unicode.rs`: Git-history classification

**Classification: INTENTIONALLY DORMANT — deliberately disabled by the upstream
author. Not a missing manifest entry. Not a forgotten experiment. Do not repair.**

`autobenches` has **never** been set in any ref, so Cargo auto-discovers
`benches/unicode.rs` with the default `harness = true`, while the file ends in
`criterion_group!` + `criterion_main!`, which requires `harness = false`. That
tension is real, but it is **author-created and author-maintained**.

Timeline:

| SHA | Date | Subject | Effect on `benches/unicode.rs` / `Cargo.toml` |
|---|---|---|---|
| `32e2be7` | 2025-10-09 | Unicode benchmarks | file created |
| `eedebcc` | 2025-10-09 | Update dependencies, make polars optional for parquet | **added** `[[bench]] name = "unicode"` |
| `6bceaac` | 2025-10-13 | **"Remove missing bench"** | despite the title, touched **only** `benches/unicode.rs` (commented out 11 further lines); did **not** remove the manifest entry |
| `f76c34b` | 2026-07-09 | Remove unused regex dependency | **commented out** the manifest entry rather than deleting it |
| `e61090f` | 2026-03-25 | Update dependencies, fix rng 0.10 usage | rng API fix |
| `cad83b0` | 2026-09-01 | Simplify the codebase: remove dead paths, dedupe, trim comments | on `simplify-refactor`, **not merged** |

Current `Cargo.toml` state (the smoking gun), from `f76c34b`:

```toml
# [[bench]]
# name = "unicode"
# harness = false
```

The author chose comment-out over deletion. The file's body is largely commented
out (39 of 88 lines at HEAD), including a `// Removed dependency since icu is ~95%
faster` note explaining the conclusion the benchmark produced. `autobenches` is
never set; no script, workflow, or document has ever referenced `--bench unicode`.

**Explicitly not repaired during provenance review**, per instruction.
Phase 5 should *measure* whether `cargo bench --no-run` / `cargo build --benches`
actually breaks on this target, and record the real diagnostic rather than
theorising.

**Note for carry-forward:** `cad83b0` (2026-09-01, branch `simplify-refactor`)
post-dates upstream `main` (2026-08-05) and touches `set_max_cache_bytes`,
`DEFAULT_MAX_CACHE_BYTES`, and `benches/unicode.rs`. It is unmerged upstream work
directly relevant to the cache audit.

---

## 7. Premise retractions and confirmations

| # | Original premise | Verdict | Evidence |
|---|---|---|---|
| 1 | "AKADATA HEAD seen externally: 34a1599" | **CONFIRMED CORRECT AND CURRENT** | `git ls-remote --symref` → `refs/heads/main` = `34a1599f0c0ae7d7cd0d1c530e6522320158b360`, dated 2026-07-25. Not stale. |
| 2 | "The current untracked tree is the AKADATA snapshot" | **RETRACTED** | Tree == `upstream/main` @ `fac0114` exactly. AKADATA is 4 commits behind. |
| 3 | "Current tree is newer than the July commit" | **CONFIRMED** | It contains `9e9271b` (2026-07-26), `7882dc8`, `aa58b2d`, `fac0114` (2026-08-05) — none of which are in `34a1599` (2026-07-25). |
| 4 | "AKADATA = snapshot + AKADATA changes / diverged branch" | **RETRACTED** | Zero unique commits, zero cherry-pick-equivalent commits, HEAD == merge-base. It is a pure unmodified mirror. |
| 5 | "Cache bounding is AKADATA's; upstream added it later" | **RETRACTED / INVERTED** | Cache bounding is **upstream's** (`9e9271b`, 2026-07-26, PR #46), landed *after* AKADATA froze. Our tree already has it. |
| 6 | "Upstream later work (esp. cache-bound) should be evaluated/merged" | **NOW VACUOUS for main** | All 4 upstream-only commits are already in our tree. Only unmerged branches remain (§8). |
| 7 | `design_doc.md` is the architecture authority | **RETRACTED** | It is titled *"Design Document for Jeton"* — a 109-line forward-looking, **unimplemented** I/O pipeline sketch. Not the implementation architecture. **Not rewritten.** |
| 8 | "No other gigatoken clone exists on this host" | **RETRACTED as unproven** | The `find -maxdepth 5` was too shallow. Only the narrower fact is established. Did not affect provenance. |

---

## 8. Permanent architectural correction

**`design_doc.md` = "Jeton" forward-looking / unimplemented design sketch.** It is
*not* the implementation architecture authority and must not be cited as such.

Current architecture authority, in order:

1. `pretokenizer_optimization_log.md`
2. `profiling/x86_port_plan.md`
3. the source code
4. the tests
5. Git history

`design_doc.md` is **not modified** during this review.

---

## 9. Machine conditions at audit time (recorded, NOT modified)

| | |
|---|---|
| Host | `homer`, Arch Linux, kernel 7.2.2-arch1-1 |
| CPU | Intel Core i9-13900K, 1 socket, **24 cores / 32 threads**, 1 NUMA node |
| ISA present | AVX2, FMA, F16C, BMI1/2, SHA-NI, VAES, GFNI, AES, AVX-VNNI |
| **ISA absent** | **AVX-512 (F/BW/VL/VBMI2)** — Raptor Lake does not expose it |
| **Governor** | **`powersave`** — recorded, **NOT changed** |
| `nproc` | 32 |
| MemTotal | 125.5 GiB |
| **MemAvailable** | **12.5 GiB** |
| Load average | 5.42 / 5.83 / 6.05 |
| GPU | RTX 3060 12 GB, driver 610.57.04, nvcc 13.4 |

~113 GiB is held by **other workloads, which were not disturbed and whose memory
was not reclaimed.**

**No benchmark conclusion will be published from this machine.** The only
benchmark activity permitted in this phase is executable/functionality smoke,
which is explicitly **not** a performance result.

**AVX-512 absence is a coverage limitation, not a blocker.** No BIOS, microcode,
or VM-spoofing attempt will be made. Homer provides valid coverage of the AVX2
runtime path, the scalar fallback, and dispatch correctness. AVX-512-specific
correctness remains a separate item for another host. Note that upstream's own
`x86_port_plan.md:307` records the AVX-512 tier as "never been timed on metal".

---

## 10. Blockers — reclassified

| ID | Original | Final status |
|---|---|---|
| B1 | nightly missing | **RESOLVED** — rustup + nightly installed user-space; nightly proven necessary and sufficient |
| B2 | powersave governor | **BENCHMARK BLOCKER ONLY** — recorded, untouched |
| B3 | 11–12 GiB available RAM | **BENCHMARK / HEAVY-FUZZ CONSTRAINT** — other work not disturbed |
| B4 | no AVX-512 | **COVERAGE LIMITATION, NOT A BLOCKER** |
| B5 | caches empty | **RESOLVED** — 210 crates + Python env provisioned |
| B6 | `benches/unicode.rs` | **INVESTIGATED — intentionally dormant, author-disabled.** Do not repair |
| B7 | maturin missing | **RESOLVED** — maturin 1.15.0 in the audit venv |

---

## 11. Standing constraints carried into Phase 5

- The `unsafe` findings (411 sites; 265 without a SAFETY comment; 129 in
  `pretokenize/reference/*`; `from_utf8_unchecked` / `unwrap_unchecked` against a
  "bytes are never validated" contract) are **LEADS, NOT FINDINGS**. For each, the
  question is whether the invariant is actually guaranteed by callers. Absence of a
  comment is not a defect. **Reproduce before assigning severity.**
- Reference/bench-only code compiled into the public `cdylib` is classified by
  **reachability**, not by filename.
- The 9 existing hand-written fuzzers are to be **run and understood first, not
  replaced**. Add `proptest` / `cargo-fuzz` / Miri only where they demonstrably add
  something; Loom only if a specific concurrency primitive warrants modelling.
- Upstream's negative optimisation record is **preserved and not re-run** — in
  particular `x86_port_plan.md:492-498` "Do not re-try" (AVX-512 masked key load,
  −36% warm / −30% cold), the AVX-512 short-merge scan (−1%), non-temporal Committer
  gather (−1%), PGO (−13%), and the hot/cold split regression (848 → 580 MiB/s).
- Any Homer performance figure is labelled **`i9-13900K / 24C32T / AVX2`** and
  reported alongside upstream's per-hardware results. The EPYC ~1000× headline is
  not to be reproduced.
- The README `~1000×` wording stays untouched during review; the 11.9 GB vs
  100 MB vs 1 GB asymmetry is **input volume**, not memory, and is disclosed by
  upstream at README:159.

---

## 12. Evidence index

| Artefact | Path |
|---|---|
| Mystery-tree manifest (SHA-256, mode, size, per entry) | `/home/smalley/src-provenance/audit/manifest-tree.tsv` |
| Tree digest | `/home/smalley/src-provenance/audit/tree-digest.txt` |
| Per-candidate manifests | `/home/smalley/src-provenance/audit/manifest-{34a1599,9e9271b,7882dc8,aa58b2d,fac0114}.tsv` |
| Combined repo holding both histories | `/home/smalley/src-provenance/akadata` (remotes: `origin`=akadata, `upstream`=marcelroed) |
| Per-commit worktrees | `/home/smalley/src-provenance/candidates/<sha>` |
| Audit Python venv (3.13.15) | `/home/smalley/src-provenance/audit-venv` |
| `portable_simd` probe file | `/tmp/gt-probe/portable_simd_probe.rs` |

Candidate worktrees contain a `.git` **file** (git's worktree indirection). This
is a checkout artefact of the comparison method, not part of the mystery tree; it
was excluded before digesting. An early diff run mislabelled it — noted so the
error is not repeated.

---

## 13. Phase 5 gate

**NOT STARTED.** Held for review per instruction. When authorised, Phase 5 runs the
untouched baseline exactly as documented: toolchain/probe records (done),
`cargo test --lib`, `cargo test`, `pytest` collect + offline-runnable subset,
`ruff`, `ty`, `maturin build`, `twine check`, bench smoke on 100 MB — with
conservative `CARGO_BUILD_JOBS`, recording every Rust test that panics on an absent
`~/data/*.txt`, and resolving B6 empirically rather than by theory.

No source changes have been made at any point in Phases 1–4.