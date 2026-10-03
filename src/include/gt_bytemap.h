/* gt_bytemap.h — the byte-level alphabet GPT-2 encodes in.
 *
 * GPT-2 does not feed raw bytes to its vocabulary. It maps every byte to a
 * printable Unicode codepoint, so that a byte-level BPE never has to represent
 * control characters in its merge table. The mapping is the one in the
 * original GPT-2 `encoder.py`:
 *
 *     bytes 0x21..0x7E, 0xA1..0xAC, 0xAE..0xFF  map to themselves
 *     every other byte, in increasing byte order, maps to 0x100, 0x101, ...
 *
 * After mapping, a pretoken is a sequence of codepoints in 0x21..0xFF and
 * 0x100..0x143. Those are the strings the merge table and the vocabulary are
 * keyed by, so this stage produces the representation every later stage uses.
 */
#ifndef GT_BYTEMAP_H
#define GT_BYTEMAP_H

#include "gt_common.h"

/* One byte -> one codepoint. Total, defined for all 256 values. */
uint32_t gt_byte_to_cp(uint8_t b);

/* Inverse. Returns -1 for a codepoint outside the alphabet, which is how a
 * vocab key that is not byte-mapped gets caught. */
int gt_cp_to_byte(uint32_t cp);

/* The number of distinct codepoints the alphabet uses. */
#define GT_ALPHABET_SIZE 256
/* Largest codepoint the alphabet can produce (0x100 + 67). */
#define GT_ALPHABET_MAX_CP 0x143

#endif /* GT_BYTEMAP_H */