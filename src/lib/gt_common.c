/* gt_common.c — buffers, file reading, status strings.
 *
 * Bounded, checked growth. Every allocation is checked: a tokenizer that
 * silently continues after a failed malloc is worse than one that stops.
 */
#include "gt_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *gt_status_str(gt_status rc) {
    switch (rc) {
        case GT_OK: return "ok";
        case GT_ERR_NULL_ARG: return "null argument";
        case GT_ERR_ALLOC: return "allocation failed";
        case GT_ERR_BAD_FORMAT: return "malformed tokenizer data";
        case GT_ERR_MISSING_DATA: return "required vocabulary entry missing";
        case GT_ERR_OOM: return "out of memory";
        case GT_ERR_IO: return "i/o error";
        case GT_ERR_STAGE_INPUT: return "stage: input";
        case GT_ERR_STAGE_PRETOK: return "stage: pretokenizer";
        case GT_ERR_STAGE_BYTEMAP: return "stage: byte mapping";
        case GT_ERR_STAGE_BPE: return "stage: bpe merges";
        case GT_ERR_STAGE_VOCAB: return "stage: vocabulary lookup";
        case GT_ERR_STAGE_SPECIAL: return "stage: special tokens";
        case GT_ERR_UNSUPPORTED: return "unsupported";
    }
    return "unknown status";
}

void gt_buf_init(gt_buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void gt_buf_free(gt_buf *b) {
    free(b->data);
    gt_buf_init(b);
}

void gt_buf_clear(gt_buf *b) {
    b->len = 0;
    if (b->data) b->data[0] = 0;
}

bool gt_buf_eq(const gt_buf *b, const char *lit) {
    size_t n = strlen(lit);
    if (b->len != n) return false;
    if (n == 0) return true;
    return memcmp(b->data, lit, n) == 0;
}

gt_status gt_buf_reserve(gt_buf *b, size_t extra) {
    /* +1 so the buffer can always hold a trailing NUL. */
    if (extra > SIZE_MAX - b->len - 1) return GT_ERR_OOM;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return GT_OK;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(b->data, cap);
    if (!p) return GT_ERR_OOM;
    b->data = p;
    b->cap = cap;
    return GT_OK;
}

gt_status gt_buf_append(gt_buf *b, const void *src, size_t n) {
    if (n == 0) return GT_OK;
    gt_status rc = gt_buf_reserve(b, n);
    if (rc != GT_OK) return rc;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = 0;
    return GT_OK;
}

gt_status gt_buf_push_byte(gt_buf *b, uint8_t v) { return gt_buf_append(b, &v, 1); }

void gt_ids_init(gt_ids *v) {
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
}

void gt_ids_free(gt_ids *v) {
    free(v->data);
    gt_ids_init(v);
}

gt_status gt_ids_reserve(gt_ids *v, size_t extra) {
    if (extra > SIZE_MAX - v->len) return GT_ERR_OOM;
    size_t need = v->len + extra;
    if (need <= v->cap) return GT_OK;
    size_t cap = v->cap ? v->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    gt_token_id *p = (gt_token_id *)realloc(v->data, cap * sizeof(*p));
    if (!p) return GT_ERR_OOM;
    v->data = p;
    v->cap = cap;
    return GT_OK;
}

void gt_ids_clear(gt_ids *v) {
    v->len = 0;
}

gt_status gt_ids_push(gt_ids *v, gt_token_id id) {
    gt_status rc = gt_ids_reserve(v, 1);
    if (rc != GT_OK) return rc;
    v->data[v->len++] = id;
    return GT_OK;
}

gt_status gt_read_file(const char *path, gt_buf *out) {
    if (!path || !out) return GT_ERR_NULL_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) return GT_ERR_IO;
    gt_buf_clear(out);
    uint8_t chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        gt_status rc = gt_buf_append(out, chunk, n);
        if (rc != GT_OK) { fclose(f); return rc; }
    }
    int bad = ferror(f);
    fclose(f);
    return bad ? GT_ERR_IO : GT_OK;
}