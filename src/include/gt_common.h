/* gt_common.h — shared types and the error convention.
 *
 * Every stage of the pipeline is a separate translation unit with a small
 * surface, so a disagreement with the Rust oracle localises to a stage rather
 * than to "the tokenizer".
 */
#ifndef GT_COMMON_H
#define GT_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A borrowed, possibly non-NUL-terminated run of bytes.
 *
 * The tokenizer's public contract, established against the Rust oracle for
 * GPT-2/r50k, is that its bytes path accepts ARBITRARY bytes: all 256 byte
 * values, invalid UTF-8, and embedded NUL. Nothing in this library may assume
 * NUL termination, and nothing may validate UTF-8 before the pretokenizer has
 * decided how to treat a byte. Lengths are explicit everywhere for that
 * reason; strlen() on user data is a bug.
 */
typedef struct {
    const uint8_t *ptr;
    size_t len;
} gt_bytes;

/* A token id, matching the oracle's u32 ids. */
typedef uint32_t gt_token_id;

/* Error codes. gt_ok is 0 so callers can `if (rc != GT_OK)`.
 *
 * `gt_err_stage` is not decoration: the differential harness reports which
 * stage rejected input, which is most of what one needs to explain a
 * mismatch.
 */
typedef enum {
    GT_OK = 0,
    GT_ERR_NULL_ARG,
    GT_ERR_ALLOC,
    GT_ERR_BAD_FORMAT,   /* tokenizer data file malformed */
    GT_ERR_MISSING_DATA, /* a required vocab/merge entry is absent */
    GT_ERR_OOM,
    GT_ERR_IO,
    GT_ERR_STAGE_INPUT,
    GT_ERR_STAGE_PRETOK,
    GT_ERR_STAGE_BYTEMAP,
    GT_ERR_STAGE_BPE,
    GT_ERR_STAGE_VOCAB,
    GT_ERR_STAGE_SPECIAL,
    GT_ERR_UNSUPPORTED
} gt_status;

/* Growable byte buffer. Used for every intermediate that needs storage. */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} gt_buf;

void gt_buf_init(gt_buf *b);
void gt_buf_free(gt_buf *b);
gt_status gt_buf_reserve(gt_buf *b, size_t extra);
gt_status gt_buf_append(gt_buf *b, const void *src, size_t n);
gt_status gt_buf_push_byte(gt_buf *b, uint8_t v);
void gt_buf_clear(gt_buf *b);

/* Compare buffer contents with a NUL-terminated literal BY LENGTH.
 *
 * Length-based on purpose: a JSON key may legally contain an escaped NUL, and
 * strcmp would treat that as a prefix match. Buffers stay NUL-terminated too
 * (see gt_buf_reserve), which keeps a bare strcmp in bounds -- this helper
 * keeps it correct. */
bool gt_buf_eq(const gt_buf *b, const char *lit);

/* Growable u32 buffer, for token ids. */
typedef struct {
    gt_token_id *data;
    size_t len;
    size_t cap;
} gt_ids;

void gt_ids_init(gt_ids *v);
void gt_ids_free(gt_ids *v);
gt_status gt_ids_reserve(gt_ids *v, size_t extra);
gt_status gt_ids_push(gt_ids *v, gt_token_id id);
void gt_ids_clear(gt_ids *v);

/* Read a whole file into a gt_buf. Length-explicit; NUL bytes are preserved. */
gt_status gt_read_file(const char *path, gt_buf *out);

const char *gt_status_str(gt_status rc);

#endif /* GT_COMMON_H */