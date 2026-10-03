/* gt_bpe.c — merge rules, rank lookup, and the merge loop.
 *
 * Rules are stored in one flat array of codepoints plus an index, and looked
 * up through an open-addressed table keyed by the concatenated pair. The table
 * is sized to a power of two and load factor stays under 1 by construction.
 * This is the plainest arrangement that gives O(1) lookup; it is not an attempt
 * at the kind of table the reference implementation uses, and deliberately so.
 */
#include "gt_bpe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_bytemap.h"
#include "gt_unicode.h"

typedef struct {
    uint32_t key_off; /* offset into gt_bpe::cps */
    uint32_t key_len; /* codepoints in left++right */
    uint32_t left_len;
} gt_rule;

struct gt_bpe {
    gt_rule *rules;
    size_t n_rules;
    uint16_t *cps; /* flat storage for every rule key */
    size_t cps_len, cps_cap;

    /* open-addressed index; slot holds rule index + 1, 0 means empty */
    uint32_t *slots;
    size_t mask;
};

/* FNV-1a over the concatenated codepoints. Any hash works; this one is short
 * and has no hidden state. */
static uint64_t hash_pair(const uint16_t *cps, gt_symbol a, gt_symbol b) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < a.len; i++) {
        h ^= cps[a.off + i];
        h *= 1099511628211ull;
    }
    /* separator, so ("ab","c") and ("a","bc") differ */
    h ^= 0xFFu;
    h *= 1099511628211ull;
    for (uint32_t i = 0; i < b.len; i++) {
        h ^= cps[b.off + i];
        h *= 1099511628211ull;
    }
    return h;
}

static gt_status bpe_push_cps(gt_bpe *b, const uint16_t *src, size_t n) {
    if (n == 0) return GT_OK;
    if (b->cps_len + n > b->cps_cap) {
        size_t cap = b->cps_cap ? b->cps_cap : 1024;
        while (cap < b->cps_len + n) cap *= 2;
        uint16_t *p = (uint16_t *)realloc(b->cps, cap * sizeof(*p));
        if (!p) return GT_ERR_OOM;
        b->cps = p;
        b->cps_cap = cap;
    }
    memcpy(b->cps + b->cps_len, src, n * sizeof(*src));
    b->cps_len += n;
    return GT_OK;
}

gt_status gt_bpe_new(const char *const *rules, size_t count, gt_bpe **out) {
    if (!out) return GT_ERR_NULL_ARG;
    /* Publish the result even on failure: a caller reusing a variable must
     * never be left holding a pointer this call already invalidated. */
    *out = NULL;
    /* Zero rules is a valid tokenizer, not an error; NULL with rules is not. */
    if (count > 0 && !rules) return GT_ERR_NULL_ARG;
    gt_bpe *b = (gt_bpe *)calloc(1, sizeof(*b));
    if (!b) return GT_ERR_OOM;

    b->rules = (gt_rule *)calloc(count ? count : 1, sizeof(gt_rule));
    if (!b->rules) {
        free(b);
        return GT_ERR_OOM;
    }

    /* Parse each "A B" rule into left++right, using one cursor over the
     * rule text so multi-byte codepoints advance correctly. */
    for (size_t i = 0; i < count; i++) {
        const char *r = rules[i];
        if (!r) { gt_bpe_free(b); return GT_ERR_BAD_FORMAT; }
        size_t rlen = strlen(r);
        const char *sp = memchr(r, ' ', rlen);
        if (!sp) { gt_bpe_free(b); return GT_ERR_BAD_FORMAT; }
        size_t left_chars = (size_t)(sp - r);
        size_t right_chars = rlen - left_chars - 1;
        if (left_chars == 0 || right_chars == 0) { gt_bpe_free(b); return GT_ERR_BAD_FORMAT; }

        uint32_t off = (uint32_t)b->cps_len;
        size_t cursor = 0;
        while (cursor < rlen) {
            if (cursor == left_chars) { /* the separating space */
                cursor++;
                continue;
            }
            size_t q = cursor;
            uint32_t cp;
            gt_cp_class cls = gt_decode_next((const uint8_t *)r, rlen, &q, &cp);
            if (cls == GT_CLS_INVALID_UTF8 || cp > GT_ALPHABET_MAX_CP) {
                gt_bpe_free(b);
                return GT_ERR_BAD_FORMAT;
            }
            uint16_t one = (uint16_t)cp;
            gt_status rc = bpe_push_cps(b, &one, 1);
            if (rc != GT_OK) { gt_bpe_free(b); return rc; }
            cursor = q;
        }
        /* left_len must be counted in CODEPOINTS, since the key length is in
         * codepoints and a merge rule's left side can be multi-byte. */
        size_t left_cps = 0;
        {
            size_t c2 = 0;
            while (c2 < left_chars) {
                size_t q2 = c2;
                uint32_t cp2;
                gt_decode_next((const uint8_t *)r, rlen, &q2, &cp2);
                c2 = q2;
                left_cps++;
            }
        }
        b->rules[i].key_off = off;
        b->rules[i].key_len = (uint32_t)(b->cps_len - off);
        b->rules[i].left_len = (uint32_t)left_cps;
        b->n_rules = i + 1;
    }

    /* Open-addressed index. Power-of-two size with load factor < 0.5. */
    size_t cap = 16;
    while (cap < count * 2) cap *= 2;
    b->slots = (uint32_t *)calloc(cap, sizeof(*b->slots));
    if (!b->slots) {
        gt_bpe_free(b);
        return GT_ERR_OOM;
    }
    b->mask = cap - 1;
    for (size_t i = 0; i < count; i++) {
        gt_symbol a = {b->rules[i].key_off, b->rules[i].left_len};
        gt_symbol c = {b->rules[i].key_off + b->rules[i].left_len,
                       b->rules[i].key_len - b->rules[i].left_len};
        uint64_t h = hash_pair(b->cps, a, c);
        size_t s = (size_t)h & b->mask;
        while (b->slots[s] != 0) s = (s + 1) & b->mask;
        b->slots[s] = (uint32_t)(i + 1);
    }
    *out = b;
    return GT_OK;
}

void gt_bpe_free(gt_bpe *b) {
    if (!b) return;
    free(b->rules);
    free(b->cps);
    free(b->slots);
    free(b);
}

size_t gt_bpe_rule_count(const gt_bpe *b) { return b ? b->n_rules : 0; }

int32_t gt_bpe_rank(const gt_bpe *b, const uint16_t *cps, gt_symbol left,
                    gt_symbol right) {
    if (!b || !cps) return -1;
    uint64_t h = hash_pair(cps, left, right);
    size_t s = (size_t)h & b->mask;
    while (b->slots[s] != 0) {
        uint32_t ri = b->slots[s] - 1;
        const gt_rule *r = &b->rules[ri];
        if (r->left_len == left.len &&
            r->key_len == left.len + right.len &&
            memcmp(b->cps + r->key_off, cps + left.off,
                   left.len * sizeof(uint16_t)) == 0 &&
            memcmp(b->cps + r->key_off + r->left_len, cps + right.off,
                   right.len * sizeof(uint16_t)) == 0) {
            return (int32_t)ri;
        }
        s = (s + 1) & b->mask;
    }
    return -1;
}

static bool sym_eq(const uint16_t *cps, gt_symbol a, gt_symbol b) {
    return a.len == b.len &&
           (a.len == 0 || memcmp(cps + a.off, cps + b.off, a.len * sizeof(uint16_t)) == 0);
}

size_t gt_bpe_merge(const gt_bpe *b, const uint16_t *cps, size_t ncps,
                    gt_symbol *syms, size_t syms_cap) {
    size_t n = 0;
    for (size_t i = 0; i < ncps; i++) {
        syms[n].off = (uint32_t)i;
        syms[n].len = 1;
        n++;
    }

    const int trace = getenv("GT_BPE_TRACE") != NULL;
    for (;;) {
        /* lowest-ranked adjacent pair */
        int32_t best = -1;
        size_t best_i = 0;
        for (size_t i = 0; i + 1 < n; i++) {
            int32_t r = gt_bpe_rank(b, cps, syms[i], syms[i + 1]);
            if (r >= 0 && (best < 0 || r < best)) {
                best = r;
                best_i = i;
            }
        }
        if (best < 0) break;

        gt_symbol A = syms[best_i];
        gt_symbol B = syms[best_i + 1];
        if (trace) {
            fprintf(stderr, "iter: n=%zu best=%d i=%zu A(off=%u len=%u) B(off=%u len=%u)\n",
                    n, (int)best, best_i, A.off, A.len, B.off, B.len);
        }

        /* Replace every leftmost non-overlapping occurrence, matching the
         * original: scan forward, on a hit consume both symbols, on a miss
         * emit one symbol and advance. */
        size_t w = 0, i = 0;
        while (i < n) {
            if (i + 1 < n && sym_eq(cps, syms[i], A) && sym_eq(cps, syms[i + 1], B)) {
                /* Take the offset from the occurrence BEING merged. A.off is
                 * the offset of the first occurrence only, and reusing it made
                 * every later occurrence alias position 0 -- which then fed a
                 * wrong symbol into the next round's pair search. */
                syms[w].off = syms[i].off;
                syms[w].len = syms[i].len + syms[i + 1].len;
                w++;
                i += 2;
            } else {
                syms[w] = syms[i];
                w++;
                i++;
            }
        }
        n = w;
        if (getenv("GT_BPE_TRACE")) {
            fprintf(stderr, "  -> n=%zu [", n);
            for (size_t k = 0; k < n; k++) fprintf(stderr, "%s(off=%u len=%u)", k?", ":"", syms[k].off, syms[k].len);
            fprintf(stderr, "]\n");
        }
        if (n <= 1) break;
    }

    (void)syms_cap;
    return n;
}