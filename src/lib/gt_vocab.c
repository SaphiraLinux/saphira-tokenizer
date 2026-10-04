/* gt_vocab.c — vocabulary as a sorted array searched by binary search.
 *
 * Entries are (codepoint slice, id). Sorting by (length, bytes) makes the
 * comparison total, so lookup never depends on insertion order and there is no
 * hash to collide.
 */
#define _GNU_SOURCE /* for qsort_r */
#include "gt_vocab.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t off;
    uint32_t len;
    gt_token_id id;
} gt_vocab_entry;

struct gt_vocab {
    gt_vocab_entry *e;
    size_t n;
    uint16_t *cps; /* flat key storage */
    size_t cps_len;
    gt_token_id max_id;
};

/* Sort by (length, bytes, id) with qsort. The comparator needs the flat key
 * storage, so it travels as qsort_r's context argument rather than a global.
 * (An earlier insertion sort was O(n^2) on 50k entries and dominated load;
 * that was a wrong algorithm, not a missed optimisation.) */
static int entry_cmp_r(const void *pa, const void *pb, void *ctx) {
    const gt_vocab *v = (const gt_vocab *)ctx;
    const gt_vocab_entry *a = (const gt_vocab_entry *)pa;
    const gt_vocab_entry *b = (const gt_vocab_entry *)pb;
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    int c = memcmp(v->cps + a->off, v->cps + b->off, a->len * sizeof(uint16_t));
    if (c != 0) return c;
    if (a->id != b->id) return a->id < b->id ? -1 : 1;
    return 0;
}

static void sort_entries(gt_vocab *v) {
    qsort_r(v->e, v->n, sizeof(gt_vocab_entry), entry_cmp_r, v);
}

gt_status gt_vocab_new(const uint16_t *keys, const size_t *key_lens,
                       const gt_token_id *ids, size_t count, gt_vocab **out) {
    if (!out) return GT_ERR_NULL_ARG;
    *out = NULL;
    if (!keys || !key_lens || !ids) return GT_ERR_NULL_ARG;
    gt_vocab *v = (gt_vocab *)calloc(1, sizeof(*v));
    if (!v) return GT_ERR_OOM;
    v->e = (gt_vocab_entry *)calloc(count ? count : 1, sizeof(gt_vocab_entry));
    if (!v->e) { free(v); return GT_ERR_OOM; }

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += key_lens[i];
    v->cps = (uint16_t *)calloc(total ? total : 1, sizeof(uint16_t));
    if (!v->cps) { free(v->e); free(v); return GT_ERR_OOM; }
    v->cps_len = total;

    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        v->e[i].off = (uint32_t)at;
        v->e[i].len = (uint32_t)key_lens[i];
        v->e[i].id = ids[i];
        if (v->e[i].id > v->max_id) v->max_id = v->e[i].id;
        at += key_lens[i];
    }
    /* Copy the key bytes: the caller's keys are laid out contiguously in the
     * same order key_lens describes. */
    at = 0;
    for (size_t i = 0; i < count; i++) {
        if (key_lens[i]) memcpy(v->cps + at, keys + at, key_lens[i] * sizeof(uint16_t));
        at += key_lens[i];
    }
    v->n = count;
    sort_entries(v);
    *out = v;
    return GT_OK;
}

void gt_vocab_free(gt_vocab *v) {
    if (!v) return;
    free(v->e);
    free(v->cps);
    free(v);
}

size_t gt_vocab_size(const gt_vocab *v) { return v ? v->n : 0; }
gt_token_id gt_vocab_max_id(const gt_vocab *v) { return v ? v->max_id : 0; }

gt_token_id gt_vocab_lookup(const gt_vocab *v, const uint16_t *cps, size_t n) {
    if (!v) return GT_ID_NONE;
    /* binary search over (len, bytes, id) */
    size_t lo = 0, hi = v->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        gt_vocab_entry e = v->e[mid];
        int c;
        if (e.len != n) {
            c = e.len < n ? -1 : 1;
        } else {
            c = memcmp(v->cps + e.off, cps, n * sizeof(uint16_t));
            if (c == 0) return e.id;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return GT_ID_NONE;
}
