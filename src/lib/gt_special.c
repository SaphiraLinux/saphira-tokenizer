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
        for (size_t i = 0; i < count; i++) {
            s->t[i] = toks[i];
            s->t[i].content.ptr = NULL;
            s->t[i].content.len = 0;
            if (toks[i].content.len && toks[i].content.ptr) {
                uint8_t *cp = (uint8_t *)malloc(toks[i].content.len);
                if (!cp) {
                    for (size_t j = 0; j < i; j++) free((void *)s->t[j].content.ptr);
                    free(s->t);
                    free(s);
                    return GT_ERR_OOM;
                }
                memcpy(cp, toks[i].content.ptr, toks[i].content.len);
                s->t[i].content.ptr = cp;
                s->t[i].content.len = toks[i].content.len;
            }
        }
        /* An empty-content token can never match; keep it for reporting but
         * exclude it from matching so it cannot match at every position. */
        s->n = count;
    }
    *out = s;
    return GT_OK;
}

void gt_special_free(gt_special *s) {
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) free((void *)s->t[i].content.ptr);
    free(s->t);
    free(s);
}

size_t gt_special_count(const gt_special *s) { return s ? s->n : 0; }

bool gt_special_find(const gt_special *s, gt_bytes input, size_t from,
                     size_t *start, size_t *len, gt_token_id *id) {
    if (!s || s->n == 0 || !start || !len || !id) return false;
    size_t bs = input.len + 1;
    size_t bl = 0;
    gt_token_id bid = 0;
    bool found = false;
    for (size_t i = 0; i < s->n; i++) {
        const gt_added_token *t = &s->t[i];
        if (!t->special || t->content.len == 0 || t->content.len > input.len) continue;
        for (size_t k = from; k + t->content.len <= input.len; k++) {
            if (memcmp(input.ptr + k, t->content.ptr, t->content.len) == 0) {
                if (!found || k < bs || (k == bs && t->content.len > bl)) {
                    bs = k;
                    bl = t->content.len;
                    bid = t->id;
                    found = true;
                }
                break;
            }
        }
    }
    if (found) {
        *start = bs;
        *len = bl;
        *id = bid;
    }
    return found;
}

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
