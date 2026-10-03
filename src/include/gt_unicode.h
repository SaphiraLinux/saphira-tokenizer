/* gt_unicode.h — codepoint classification for the pretokenizer.
 *
 * The GPT-2 pattern needs three predicates: \p{L}, \p{N} and \s. The tables
 * are generated from the Unicode Character Database by
 * tools/gen_unicode_tables.py, not taken from any tokenizer implementation.
 */
#ifndef GT_UNICODE_H
#define GT_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t lo;
    uint32_t hi;
} gt_cp_range;

/* Defined in the generated gt_unicode_tables.c */
extern const gt_cp_range gt_cp_ranges_letter[];
extern const size_t gt_cp_ranges_letter_count;
extern const gt_cp_range gt_cp_ranges_number[];
extern const size_t gt_cp_ranges_number_count;
extern const gt_cp_range gt_cp_ranges_space[];
extern const size_t gt_cp_ranges_space_count;

bool gt_cp_is_letter(uint32_t cp);
bool gt_cp_is_number(uint32_t cp);
bool gt_cp_is_space(uint32_t cp);

/* The three classes the pattern distinguishes, kept separate so the
 * pretokenizer reads as the pattern does. */
typedef enum {
    GT_CLS_OTHER = 0,
    GT_CLS_LETTER,
    GT_CLS_NUMBER,
    GT_CLS_SPACE,
    GT_CLS_INVALID_UTF8
} gt_cp_class;

/* Classify one codepoint. INVALID_UTF8 is not one of the three pattern
 * classes: the decoder uses it to mark a byte sequence it could not decode,
 * which the pretokenizer then treats as an "other" character while still
 * honouring how many bytes it consumed. */
gt_cp_class gt_cp_classify(uint32_t cp);

/* Decode one codepoint from `bytes[*pos .. len).
 *
 * Advances *pos past the sequence consumed. An undecodable byte consumes
 * exactly one byte and yields GT_CLS_INVALID_UTF8 — the reference
 * implementation's contract, verified against the oracle, which encodes
 * b"a\xffb" as three ids (one per byte), not two.
 *
 * Never reads past `len`, and never rejects: arbitrary bytes are legal input
 * for this tokenizer family.
 */
gt_cp_class gt_decode_next(const uint8_t *bytes, size_t len, size_t *pos,
                           uint32_t *cp_out);

/* Encode a codepoint as UTF-8 into `out` (needs 4 bytes); returns length. */
size_t gt_encode_utf8(uint32_t cp, uint8_t *out);

#endif /* GT_UNICODE_H */