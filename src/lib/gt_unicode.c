/* gt_unicode.c — codepoint classification and decoding.
 *
 * Tables are generated (see tools/gen_unicode_tables.py); this file is the
 * lookup and the decoder. Both are written as the obvious thing, because the
 * point of this implementation is to be checkable against another
 * implementation, not to be fast.
 */
#include "gt_unicode.h"

#include <string.h>

/* Binary search over sorted inclusive ranges. */
static bool in_ranges(const gt_cp_range *rs, size_t n, uint32_t cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < rs[mid].lo) {
            hi = mid;
        } else if (cp > rs[mid].hi) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool gt_cp_is_letter(uint32_t cp) {
    return in_ranges(gt_cp_ranges_letter, gt_cp_ranges_letter_count, cp);
}

bool gt_cp_is_number(uint32_t cp) {
    return in_ranges(gt_cp_ranges_number, gt_cp_ranges_number_count, cp);
}

bool gt_cp_is_space(uint32_t cp) {
    return in_ranges(gt_cp_ranges_space, gt_cp_ranges_space_count, cp);
}

gt_cp_class gt_cp_classify(uint32_t cp) {
    if (gt_cp_is_space(cp)) return GT_CLS_SPACE;
    if (gt_cp_is_letter(cp)) return GT_CLS_LETTER;
    if (gt_cp_is_number(cp)) return GT_CLS_NUMBER;
    return GT_CLS_OTHER;
}

size_t gt_encode_utf8(uint32_t cp, uint8_t *out) {
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (uint8_t)(0xF0 | (cp >> 18));
    out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
}

/* How many bytes a well-formed sequence starting with `lead` would occupy, or
 * 0 if `lead` cannot start a sequence. */
static size_t seq_len(uint8_t lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

/* Decode one codepoint, advancing *pos.
 *
 * Invalid input consumes exactly ONE byte and reports GT_CLS_INVALID_UTF8. This
 * is the behaviour the oracle shows: b"a\xffb" encodes to three ids, one per
 * byte, so an undecodable byte is a character of its own rather than being
 * dropped or swallowing a neighbour.
 *
 * Surrogates and overlong forms are rejected the same way, which is why the
 * checks below are explicit rather than merely "is the next byte a
 * continuation".
 */
gt_cp_class gt_decode_next(const uint8_t *bytes, size_t len, size_t *pos,
                           uint32_t *cp_out) {
    size_t i = *pos;
    if (i >= len) {
        *cp_out = 0;
        return GT_CLS_OTHER;
    }
    uint8_t b0 = bytes[i];
    size_t n = seq_len(b0);
    if (n == 0) {
        /* continuation byte in lead position, or 0xC0/0xC1/0xF5..0xFF */
        *pos = i + 1;
        *cp_out = 0xFFFD;
        return GT_CLS_INVALID_UTF8;
    }
    if (n == 1) {
        *pos = i + 1;
        *cp_out = b0;
        return gt_cp_classify(b0);
    }
    if (i + n > len) {
        /* truncated at the end of the buffer */
        *pos = i + 1;
        *cp_out = 0xFFFD;
        return GT_CLS_INVALID_UTF8;
    }
    for (size_t k = 1; k < n; k++) {
        if ((bytes[i + k] & 0xC0) != 0x80) {
            *pos = i + 1;
            *cp_out = 0xFFFD;
            return GT_CLS_INVALID_UTF8;
        }
    }
    uint32_t cp;
    switch (n) {
        case 2: cp = ((uint32_t)(b0 & 0x1F) << 6) | (bytes[i + 1] & 0x3F); break;
        case 3: cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)(bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F); break;
        default: cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)(bytes[i + 1] & 0x3F) << 12) |
                      ((uint32_t)(bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F); break;
    }
    /* Reject overlong encodings, surrogates and out-of-range, the same set a
     * strict decoder rejects. */
    static const uint32_t min_for[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < min_for[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *pos = i + 1;
        *cp_out = 0xFFFD;
        return GT_CLS_INVALID_UTF8;
    }
    *pos = i + n;
    *cp_out = cp;
    return gt_cp_classify(cp);
}