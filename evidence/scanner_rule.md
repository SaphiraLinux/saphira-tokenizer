# Scanner consume/classify rule — DIRECTLY TRACED (GT_SCAN, debug/scanner-trace)

Traced with a forensic emitter on `decode_cp` (both paths) + `advance_pos`.
No token ids, no BPE, no span inference. Source of truth = the trace.

## Width proposal, by lead byte
| byte      | shape                    | proposed width |
|-----------|--------------------------|----------------|
| 80..BF    | continuation-looking     | 3 (!)          |
| C0..DF    | 2-byte lead-shaped       | 2              |
| E0..EF    | 3-byte lead-shaped       | 3              |
| F0..F4    | 4-byte lead-shaped       | 4              |
| F5..FF    | not a lead               | 1 (no decode) |

The `80..BF -> 3` row is an artefact of branch structure, not intent: the fix's
width test is `b0 < 0x80 || (b0 >= 0xC0 && b0 < 0xE0)`, so the continuation
band falls through into the 3-byte arm. It is masked in-bounds by the
continuation check, but it is observable near the buffer end (see below).

## inbounds path (pos + 4 <= len)
Validate that bytes `1..width` are all `0b10xxxxxx`.
- all valid  -> assemble, width = proposal, class from the ASSEMBLED value
- any invalid -> `(CP_INVALID, 1)` = (U+10FFFF, 1), class O

    f0 90 80 80 -> w=4 cp=U+10000 cls=L     f5 80 80 80 -> w=1 cp=U+10FFFF cls=O
    <any lead-shaped byte> + 'aaaa'  -> w=1 cp=U+10FFFF cls=O   (all 14 probed)

## near_end path (pos + 4 > len)
- proposal > bytes remaining -> `(CP_INVALID, remaining)`, i.e. EAT THE REST
- else validate continuations; invalid -> `(CP_INVALID, 1)`

    80 a9      need=3 rem=2 -> w=2  cp=U+10FFFF cls=O   <- continuation band eats 2
    bd 40      need=3 rem=2 -> w=2  cp=U+10FFFF cls=O   <- ditto
    c2 a9      need=2 rem=2 -> w=2  cp=U+00A9  cls=O
    e0 a0 80   need=3 rem=3 -> w=3  cp=U+0800  cls=L
    e9 80 80   need=3 rem=3 -> w=3  cp=U+9000  cls=L
    ef bf bf   need=3 rem=3 -> w=3  cp=U+FFFF  cls=O
    c0 af      need=2 rem=2 -> w=2  cp=U+002F  cls=O   <- OVERLONG assembled
    ed a0 80   need=3 rem=3 -> w=3  cp=U+D800  cls=O   <- SURROGATE assembled
    every byte at EOF         -> w=1  cp=U+10FFFF cls=O

Class comes from the assembled value, so structurally decodable != valid scalar:
an overlong `/` and a surrogate U+D800 are both assembled and classified as Other.

## CP_INVALID is U+10FFFF and classifies as Other

So every malformed lead currently collapses to Other. **This is where Rust is
still wrong**, and it is the `bd9b9ae9` defect:

    b'\xef@yp'       trace: 0xEF -> w=1 U+10FFFF O  => coalesces with '@'(O)  [ef40]
    b'\xbd\x9b\x9a\xe9' reference: 0xBD is '½' = No, 0x9B/0x9A/0xE9 are letters
                                   => [bd] as a NUMBER run, then [9b9ae9] letters
    Rust trace for 0xBD: w=1 U+10FFFF O  -> all Other -> [bd9b9a][e9]   WRONG

**0xBD is not lead-shaped at all.** It sits in the continuation band, so no
lead arm was ever entered and the correct fallback is the BYTE-ALPHABET symbol's
own category ('½' -> No), not blanket Other. 0xEF *is* lead-shaped, so its
malformed-lead path (Other) is correct and the reference agrees.

=> The landed fix e749f8f is INCOMPLETE. Its fallback conflates two populations:
   - non-lead-shaped bytes (80..BF, F5..FF): fall back to the byte-alphabet
     symbol's Unicode category
   - lead-shaped bytes (C0..F4) that fail validation: CP_INVALID -> Other

CONFIRMED only for 0x89/0x88/0x81 (letters, from forensic case CONT) and 0xBD
(No). The byte-alphabet fallback across the whole 80..BF and F5..FF bands still
needs an exhaustive sweep before the C is written. Do not assume uniformity.

---

# Exhaustive fallback sweep, 80..BF and F5..FF (direct trace)

Property under test: **no following byte sequence can cause width > 1.**

| bait after the byte | 80..BF (64 bytes) | F5..FF (11 bytes) |
|---|---|---|
| isolated          | w=1 (all 64) | w=1 (all 11) |
| + ASCII 0x61      | **w=2 (all 64)** | w=1 (all 11) |
| + 1 continuation  | **w=2 (all 64)** | w=1 (all 11) |
| + 2 continuations | **w=3 (all 64)** cp=U+0000 | w=1 (all 11) |
| + 3 continuations | **w=3 (all 64)** | w=1 (all 11) |
| + 4 continuations | **w=3 (all 64)** | w=1 (all 11) |

## 80..BF: property VIOLATED universally

All 64 bytes, in every non-isolated configuration, can be made to consume 2 or 3
bytes. Two distinct mechanisms:

1. `+ASCII` / `+1 continuation` -> w=2 via the near_end "proposal exceeds
   remaining, eat the rest" branch (proposal is 3, remaining is 2).
2. `+2 continuations` -> w=3 with an ASSEMBLED value. Three continuation bytes
   assemble to U+0000, i.e. `80 80 80` is read as the character NUL.

So `80..BF` reaching the width-3 arm is not a rare boundary accident. It is
reachable for every byte in the band under ordinary input, and it is what
produces the forensic failures (`89 88 81` -> U+9201, `BD 9B 9A` -> U+D6DA).

Required permanent regression: for all 64 bytes, width == 1 under every bait.

## F5..FF: property HOLDS

All 11 bytes return w=1 under every bait tested, isolated or not. `F5..FF`
already behaves as required. **Do not assume it shares `80..BF`'s fate — it does
not, and the two bands need separate proofs.** This also retroactively confirms
the `b0 < 0xF5` guard in `e749f8f` is doing real work.

## Corrected contract

    00..7F  ordinary byte, existing ASCII handling (0x20 stays whitespace)
    80..BF  NEVER a lead; consume exactly 1;
            class = category of that byte's GPT-2 byte-alphabet symbol
    C0..DF  propose 2 | E0..EF propose 3 | F0..F4 propose 4
            continuations valid   -> assemble, classify the assembled value
            continuations invalid -> CP_INVALID / Other
    F5..FF  never a lead; consume 1 (behaviour already correct)

The byte-alphabet class must come from ONE auditable derivation —
byte -> byte-alphabet codepoint -> the existing Unicode-category classifier —
not a hand-maintained 256-entry table. A generated 256-entry table is acceptable
only if generated from that rule and tested exhaustively against the derivation.
Whitespace stays a byte-level property of 0x20; do not generalise the fallback
onto ASCII.

## Not yet done
`80..BF` fallback class per byte is still only spot-checked (0x89/0x9B letter,
0xBD number, 0xAB other). The per-byte class table for all 64 must be swept
against the reference before implementing.
