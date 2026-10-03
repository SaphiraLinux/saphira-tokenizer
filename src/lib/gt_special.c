/* gt_special.c — added-token matching.
 *
 * Plain longest-first substring search. For GPT-2 the set is empty or
 * degenerate, so the interesting behaviour is the *policy*: match before
 * pretokenizing, never split a match across pretoken boundaries, and treat an
 * empty-content token as matching nothing.
 */
#include "gt_special.h"

#include <stdlib.h>
#include <string.h>

struct gt_special {
    gt_added_token *t;
    size_t n;
};

gt_status gt_special_new(const gt_added_token *toks, size_t count, gt_special **out) {
    if (!out) return GT_ERR_NULL_ARG;
    *out = NULL;
    gt_special *s = (gt_special *)calloc(1, sizeof(*s));
    if (!s) return GT_ERR_OOM;
    if (count) {
        s->t = (gt_added_token *)calloc(count, sizeof(gt_added_token));
        if (!s->t) { free(s); return GT_ERR_OOM; }
        memcpy(s->t, toks, count * sizeof(gt_added_token));
        /* An empty-content token can never match; keep it for reporting but
         * exclude it from matching so it cannot match at every position. */
        s->n = count;
    }
    *out = s;
    return GT_OK;
}

void gt_special_free(gt_special *s) {
    if (!s) return;
    free(s->t);
    free(s);
}

size_t gt_special_count(const gt_special *s) { return s ? s->n : 0; }

bool gt_special_contains(const gt_special *s, gt_bytes input) {
    if (!s || s->n == 0) return false;
    for (size_t i = 0; i < s->n; i++) {
        const gt_added_token *t = &s->t[i];
        if (!t->special) continue;
        if (t->content.len == 0) continue; /* matches nothing, by definition */
        if (t->content.len > input.len) continue;
        for (size_t k = 0; k + t->content.len <= input.len; k++) {
            if (memcmp(input.ptr + k, t->content.ptr, t->content.len) == 0) {
                return true;
            }
        }
    }
    return false;
}
