/* gt_special.h — added/special token policy.
 *
 * A separate stage, kept separate deliberately: a mismatch here is otherwise
 * indistinguishable from a merge or vocabulary mismatch, and for GPT-2 the
 * added-token set is a genuine edge case rather than a formality. In the
 * GPT-2 vocabulary the single added token has the EMPTY string as its content
 * and id 50256, which both the reference implementation and the Rust oracle
 * agree does not match anything. That is recorded here as an explicit,
 * checkable fact rather than left implicit.
 */
#ifndef GT_SPECIAL_H
#define GT_SPECIAL_H

#include "gt_common.h"

typedef struct {
    gt_bytes content;
    gt_token_id id;
    bool special;
} gt_added_token;

typedef struct gt_special gt_special;

gt_status gt_special_new(const gt_added_token *toks, size_t count, gt_special **out);
void gt_special_free(gt_special *s);
size_t gt_special_count(const gt_special *s);

/* Does this input contain a special token at all? Checked before
 * pretokenization so a special token is never split by the pattern. */
bool gt_special_contains(const gt_special *s, gt_bytes input);

/* Leftmost special-token match at or after `from`. Longest wins when several
 * start at the same position. Returns true and fills start/len/id; false if
 * none. Empty-content and non-special entries never match. */
bool gt_special_find(const gt_special *s, gt_bytes input, size_t from,
                     size_t *start, size_t *len, gt_token_id *id);

#endif /* GT_SPECIAL_H */
