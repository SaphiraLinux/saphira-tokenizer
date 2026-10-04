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
