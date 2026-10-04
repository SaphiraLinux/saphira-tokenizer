/* gt_pretok.h — the pretokenizer: split a byte string into GPT-2 pieces.
 *
 * The pattern, leftmost-first:
 *
 *   's|'t|'re|'ve|'m|'ll|'d
 *   | ?\p{L}+
 *   | ?\p{N}+
 *   | ?[^\s\p{L}\p{N}]+
 *   | \s+(?!\S)
 *   | \s+
 *
 * A pretoken is a (ptr,len) view into the caller's buffer. No allocation, no
 * copying, and no assumption that the bytes are text: the family accepts
 * arbitrary bytes, and an undecodable byte participates in the pattern as an
 * "other" character, one byte at a time.
 */
#ifndef GT_PRETOK_H
#define GT_PRETOK_H

#include "gt_common.h"
#include "gt_unicode.h"

/* Iterate pretokens. `pos` is the caller's cursor. */
typedef struct {
    const uint8_t *base;
    size_t len;
    size_t pos;
} gt_pretok_iter;

void gt_pretok_iter_init(gt_pretok_iter *it, gt_bytes input);

/* The scanner's per-position decision, published so tests can assert the
 * contract directly instead of inferring it through BPE: the class and the
 * number of bytes consumed at `pos`. `adv` is always >= 1. */
typedef enum { GT_PCLS_LETTER, GT_PCLS_NUMBER, GT_PCLS_SPACE, GT_PCLS_OTHER } gt_pcls;
gt_pcls gt_pretok_class_at(const uint8_t *b, size_t len, size_t pos, size_t *adv);

/* Next pretoken, or 0 when the input is exhausted.
 *
 * `out` receives a view into the original buffer. Returns 1 and sets out on
 * success, 0 at end of input. Never returns an empty pretoken: the pattern
 * cannot match empty, and a caller that received one would loop forever.
 */
int gt_pretok_next(gt_pretok_iter *it, gt_bytes *out);

#endif /* GT_PRETOK_H */