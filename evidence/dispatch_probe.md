# Dispatch-boundary probe (gigatoken_rs, corrected Rust, GPT-2/r50k)

Question: does the public bytes API pick ONE pipeline for the whole buffer
(valid-UTF-8 -> TEXT, else -> BYTES), or switch/fall back per region?

    case                      hex                       spans
    ascii only                68656c6c6f20776f726c64    [hello][ world]
    valid multibyte           636166c3a920e697a5e69cac  [636166c3a9][20e697a5e69cac]
    valid + invalid mid       636166c3a9ff20776f726c64  [636166c3a9][ff][20776f726c64]
    invalid + valid suffix    ff636166c3a9              [ff][636166c3a9]
    truncated seq at EOF      636166c3                  [636166][c3]
    lone continuation         80                        [80]
    overlong encoding         c0af                      [c0af]
    surrogate encoding        eda080                    [eda080]
    beyond U+10FFFF           f5808080                  [f5808080]
    embedded NUL              610062                    [61][00][62]
    valid+NUL+valid           636166c3a900e697a5        [636166c3a9][00][e697a5]

## CONCLUSION: there is no whole-buffer dispatch and no fallback.

1. Valid multibyte stays WHOLE inside buffers that are NOT valid UTF-8
   (`valid + invalid mid`, `invalid + valid suffix`, `valid+NUL+valid`).
   A whole-buffer fallback to a byte pipeline would have shredded the C3 A9 in
   all three. It does not. So the choice is NOT made per buffer.
2. `embedded NUL` splits NUL into its own span and keeps valid text whole on
   both sides -> NUL is not a whole-buffer poison.
3. The pipeline is therefore a SINGLE byte-walking classifier: advance by the
   width of whatever it can decode at each position, and classify that symbol.
   There is no "TEXT mode" and no "BYTES mode" to switch between.
4. Overlong (c0af), surrogate (eda080) and >U+10FFFF (f5808080) sequences are
   each kept as ONE span of their full nominal width, i.e. they are decoded and
   classified from the assembled codepoint, not rejected byte-by-byte.

## OPEN QUESTION (must be resolved before writing the C)

Two high-byte cases pull in opposite directions and both are authoritative:

    b'\xef@yp'    -> [ef40][7970]      'EF' and '@' in ONE span  => 'ï' behaves as Other
    b'\xbd\x9b\x9a\xe9' -> [bd][9b9ae9] 'BD' is a NUMBER run, then letters

Both are bytes >= 0x80 classified via the byte alphabet, yet they disagree about
whether the byte's own mapped codepoint supplies the class ('ï'=U+00EF is a
letter; '½'=U+00BD is a number) or whether the decode result supplies it.

Candidate discriminating rules to test:
  (a) self-mapped high bytes (0xAE..0xFF -> their Latin-1 char) vs non-self
      high bytes (0x80..0xA0, 0xAD -> U+0100..U+0143) are classified
      differently;
  (b) a lead byte whose continuations are INVALID is Other, whereas a lead byte
      that is merely TRUNCATED at EOF is classified by its byte-map codepoint
      (this is exactly the 0xEF-vs-0xE9 difference between the two cases);
  (c) the reference transcription itself is wrong for one of them.

Do NOT guess. Rule (b) is the most likely, since it is the only distinction
that separates the two cases while keeping every other observation intact, but
it must be proven by exhaustive sweep before any C is written.
