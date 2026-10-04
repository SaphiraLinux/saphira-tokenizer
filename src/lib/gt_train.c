/* gt_train.c — byte-level BPE training.
 *
 * Words are deduplicated pretokens with frequencies. Symbols start as
 * byte-alphabet ids; each round merges the most frequent adjacent pair
 * everywhere it occurs. Only words containing that pair are touched, and the
 * best pair comes from a heap, so cost is per affected word, not per corpus.
 *
 * Staleness is handled by verification, not by eager cleanup: a heap entry is
 * checked against the live pair count when popped, and a word listed under a
 * pair is scanned for a real occurrence before merging. A stale entry costs a
 * scan that finds nothing; it can never corrupt a count.
 */
#include "gt_train.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_bytemap.h"
#include "gt_pretok.h"

/* ---- words ------------------------------------------------------------ */

typedef struct {
    uint32_t *syms;
    size_t n, cap;
    uint64_t count;
} gt_word;

static void word_free(gt_word *w) {
    free(w->syms);
    w->syms = NULL;
    w->n = w->cap = 0;
}

/* ---- pair table: (a,b) -> count + words containing it ------------------ */

typedef struct {
    uint8_t used;
    uint32_t a, b;
    uint64_t count;
    uint32_t *words;
    size_t nwords, capwords;
} gt_pentry;

typedef struct {
    gt_pentry *e;
    size_t cap, mask, n;
} gt_ptab;

static uint64_t phash(uint32_t a, uint32_t b) {
    uint64_t h = (uint64_t)a * 0x9E3779B97F4A7C15ull + b;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    return h;
}

static gt_status ptab_init(gt_ptab *t, size_t cap) {
    size_t c = 64;
    while (c < cap) c *= 2;
    t->e = (gt_pentry *)calloc(c, sizeof(gt_pentry));
    if (!t->e) return GT_ERR_OOM;
    t->cap = c;
    t->mask = c - 1;
    t->n = 0;
    return GT_OK;
}

static void ptab_free(gt_ptab *t) {
    if (!t->e) return;
    for (size_t i = 0; i < t->cap; i++) free(t->e[i].words);
    free(t->e);
    t->e = NULL;
}

/* Double the table when load passes 0.7. Entries (which own their word
 * lists) move, so no gt_pentry* may be held across a call that can grow;
 * callers re-fetch after any nested insert. */
static gt_status ptab_grow(gt_ptab *t) {
    size_t nc = t->cap * 2;
    gt_pentry *ne = (gt_pentry *)calloc(nc, sizeof(gt_pentry));
    if (!ne) return GT_ERR_OOM;
    size_t nmask = nc - 1;
    for (size_t i = 0; i < t->cap; i++) {
        if (!t->e[i].used) continue;
        size_t sl = (size_t)phash(t->e[i].a, t->e[i].b) & nmask;
        while (ne[sl].used) sl = (sl + 1) & nmask;
        ne[sl] = t->e[i];
    }
    free(t->e);
    t->e = ne;
    t->cap = nc;
    t->mask = nmask;
    return GT_OK;
}

static gt_pentry *ptab_get(gt_ptab *t, uint32_t a, uint32_t b, bool create) {
    /* Grow-before-insert keeps at least one empty slot, so the probe below
     * always terminates. Read-only lookups never grow. */
    if (create && (t->n + 1) * 10 >= t->cap * 7) {
        if (ptab_grow(t) != GT_OK) return NULL;
    }
    size_t s = (size_t)phash(a, b) & t->mask;
    while (t->e[s].used) {
        if (t->e[s].a == a && t->e[s].b == b) return &t->e[s];
        s = (s + 1) & t->mask;
    }
    if (!create) return NULL;
    t->e[s].used = 1;
    t->e[s].a = a;
    t->e[s].b = b;
    t->e[s].count = 0;
    t->e[s].words = NULL;
    t->e[s].nwords = t->e[s].capwords = 0;
    t->n++;
    return &t->e[s];
}

static gt_status pentry_add_word(gt_pentry *e, uint32_t w) {
    if (e->nwords == e->capwords) {
        size_t nc = e->capwords ? e->capwords * 2 : 4;
        uint32_t *p = (uint32_t *)realloc(e->words, nc * sizeof(uint32_t));
        if (!p) return GT_ERR_OOM;
        e->words = p;
        e->capwords = nc;
    }
    e->words[e->nwords++] = w;
    return GT_OK;
}

/* ---- max-heap of (count, a, b) with lazy deletion ----------------------- */

typedef struct {
    uint64_t count;
    uint32_t a, b;
} gt_hent;

typedef struct {
    gt_hent *e;
    size_t n, cap;
} gt_heap;

static gt_status heap_push(gt_heap *h, uint64_t count, uint32_t a, uint32_t b) {
    if (h->n == h->cap) {
        size_t nc = h->cap ? h->cap * 2 : 1024;
        gt_hent *p = (gt_hent *)realloc(h->e, nc * sizeof(gt_hent));
        if (!p) return GT_ERR_OOM;
        h->e = p;
        h->cap = nc;
    }
    size_t i = h->n++;
    h->e[i].count = count;
    h->e[i].a = a;
    h->e[i].b = b;
    while (i > 0) {
        size_t par = (i - 1) / 2;
        /* Higher count wins; ties break to the lower (a, b) so order is
         * deterministic and documented. */
        bool above = h->e[i].count > h->e[par].count ||
                     (h->e[i].count == h->e[par].count &&
                      (h->e[i].a < h->e[par].a ||
                       (h->e[i].a == h->e[par].a && h->e[i].b < h->e[par].b)));
        if (!above) break;
        gt_hent t = h->e[i];
        h->e[i] = h->e[par];
        h->e[par] = t;
        i = par;
    }
    return GT_OK;
}

static bool heap_pop(gt_heap *h, uint64_t *count, uint32_t *a, uint32_t *b) {
    if (h->n == 0) return false;
    *count = h->e[0].count;
    *a = h->e[0].a;
    *b = h->e[0].b;
    h->n--;
    if (h->n > 0) {
        h->e[0] = h->e[h->n];
        size_t i = 0;
        for (;;) {
            size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < h->n &&
                (h->e[l].count > h->e[m].count ||
                 (h->e[l].count == h->e[m].count &&
                  (h->e[l].a < h->e[m].a ||
                   (h->e[l].a == h->e[m].a && h->e[l].b < h->e[m].b)))))
                m = l;
            if (r < h->n &&
                (h->e[r].count > h->e[m].count ||
                 (h->e[r].count == h->e[m].count &&
                  (h->e[r].a < h->e[m].a ||
                   (h->e[r].a == h->e[m].a && h->e[r].b < h->e[m].b)))))
                m = r;
            if (m == i) break;
            gt_hent t = h->e[i];
            h->e[i] = h->e[m];
            h->e[m] = t;
            i = m;
        }
    }
    return true;
}

/* ---- trainer state ------------------------------------------------------ */

typedef struct {
    gt_word *words;
    size_t nwords, capwords;
    gt_ptab pairs;
    gt_heap heap;
    /* symbol id -> byte string, for emitting the vocabulary */
    gt_buf *symtext;
    size_t nsymtext, capsymtext;
    uint32_t next_id;
} gt_trainer;

static gt_status trainer_symtext(gt_trainer *tr, uint32_t id, const uint8_t *p, size_t n) {
    if (id >= tr->capsymtext) {
        size_t nc = tr->capsymtext ? tr->capsymtext * 2 : 512;
        while (nc <= id) nc *= 2;
        gt_buf *q = (gt_buf *)realloc(tr->symtext, nc * sizeof(gt_buf));
        if (!q) return GT_ERR_OOM;
        for (size_t i = tr->capsymtext; i < nc; i++) gt_buf_init(&q[i]);
        tr->symtext = q;
        tr->capsymtext = nc;
    }
    if (id >= tr->nsymtext) tr->nsymtext = id + 1;
    gt_buf_clear(&tr->symtext[id]);
    return gt_buf_append(&tr->symtext[id], p, n);
}

/* Index (don't count yet) every adjacent pair of a word. */
static gt_status word_index_pairs(gt_trainer *tr, uint32_t wi) {
    gt_word *w = &tr->words[wi];
    for (size_t i = 0; i + 1 < w->n; i++) {
        gt_pentry *e = ptab_get(&tr->pairs, w->syms[i], w->syms[i + 1], true);
        if (!e) return GT_ERR_OOM;
        gt_status rc = pentry_add_word(e, wi);
        if (rc != GT_OK) return rc;
    }
    return GT_OK;
}

/* Count (first pass) every adjacent pair of a word, weighted by frequency. */
static gt_status word_count_pairs(gt_trainer *tr, uint32_t wi) {
    gt_word *w = &tr->words[wi];
    for (size_t i = 0; i + 1 < w->n; i++) {
        gt_pentry *e = ptab_get(&tr->pairs, w->syms[i], w->syms[i + 1], true);
        if (!e) return GT_ERR_OOM;
        e->count += w->count;
    }
    return GT_OK;
}

void gt_trained_free(gt_trained *t) {
    if (!t) return;
    for (size_t i = 0; i < t->ntoks; i++) gt_buf_free(&t->toks[i].bytes);
    free(t->toks);
    for (size_t i = 0; i < t->nmerges; i++) {
        gt_buf_free(&t->merges[i].left);
        gt_buf_free(&t->merges[i].right);
    }
    free(t->merges);
    t->toks = NULL;
    t->merges = NULL;
    t->ntoks = t->nmerges = 0;
}

gt_status gt_train(const gt_bytes *docs, size_t ndocs, size_t vocab_size,
                   const gt_bytes *specials, size_t nspecial, gt_trained *out) {
    if (!docs || !out) return GT_ERR_NULL_ARG;
    memset(out, 0, sizeof *out);

    gt_trainer tr;
    memset(&tr, 0, sizeof tr);
    gt_status rc = ptab_init(&tr.pairs, 4096);
    if (rc != GT_OK) return rc;

    /* Seed the 256 byte symbols. */
    for (uint32_t b = 0; b < 256; b++) {
        uint8_t one = (uint8_t)b;
        rc = trainer_symtext(&tr, b, &one, 1);
        if (rc != GT_OK) goto fail;
    }
    tr.next_id = 256;

    /* Split into pretokens, deduplicate into words with frequencies. */
    for (size_t d = 0; d < ndocs; d++) {
        gt_pretok_iter it;
        gt_pretok_iter_init(&it, docs[d]);
        gt_bytes piece;
        while (gt_pretok_next(&it, &piece)) {
            if (piece.len == 0 || piece.len > 15) continue;
            /* Dedup by byte text. Words that have already merged hold symbols
             * above 255, so they can never equal a fresh byte-level piece;
             * length plus byte equality is exact here. Linear scan is fine:
             * the pair machinery below is where scale matters, not this. */
            size_t found = tr.nwords;
            for (size_t k = 0; k < tr.nwords; k++) {
                /* words store symbols; length-1 words started as single bytes.
                 * Compare against the piece bytes via symtext only when the
                 * word is still all single-byte symbols. General case: keep
                 * it simple and compare symbol-by-symbol mapped back. */
                gt_word *w = &tr.words[k];
                if (w->n != piece.len) continue;
                bool same = true;
                for (size_t q = 0; q < piece.len; q++) {
                    /* a single-byte symbol's text is its byte */
                    if (w->syms[q] >= 256) { same = false; break; }
                    if ((uint8_t)w->syms[q] != piece.ptr[q]) { same = false; break; }
                }
                if (same) { found = k; break; }
            }
            /* Words that have already merged are never equal to a fresh
             * byte-level piece (their symbols exceed 255), so length+byte
             * comparison is exact here. */
            if (found < tr.nwords) {
                tr.words[found].count++;
                continue;
            }
            if (tr.nwords == tr.capwords) {
                size_t nc = tr.capwords ? tr.capwords * 2 : 1024;
                gt_word *p = (gt_word *)realloc(tr.words, nc * sizeof(gt_word));
                if (!p) { rc = GT_ERR_OOM; goto fail; }
                tr.words = p;
                tr.capwords = nc;
            }
            gt_word *w = &tr.words[tr.nwords++];
            w->n = w->cap = 0;
            w->syms = NULL;
            w->count = 1;
            if (piece.len) {
                w->syms = (uint32_t *)malloc(piece.len * sizeof(uint32_t));
                if (!w->syms) { rc = GT_ERR_OOM; goto fail; }
                for (size_t q = 0; q < piece.len; q++)
                    w->syms[q] = gt_byte_to_cp(piece.ptr[q]) < 256
                                     ? (uint32_t)piece.ptr[q]
                                     : 256 + (uint32_t)piece.ptr[q];
                /* Symbols are byte ids in 0..255: the byte alphabet's index,
                 * which is what merges rank over. Map through the byte value
                 * itself, not the mapped codepoint, because training counts
                 * byte pairs. */
                for (size_t q = 0; q < piece.len; q++) w->syms[q] = piece.ptr[q];
                w->n = w->cap = piece.len;
            }
        }
    }

    /* Index then count all pairs, and seed the heap. */
    for (uint32_t wi = 0; wi < tr.nwords; wi++) {
        rc = word_index_pairs(&tr, wi);
        if (rc != GT_OK) goto fail;
    }
    for (uint32_t wi = 0; wi < tr.nwords; wi++) {
        rc = word_count_pairs(&tr, wi);
        if (rc != GT_OK) goto fail;
    }
    for (size_t i = 0; i < tr.pairs.cap; i++) {
        gt_pentry *e = &tr.pairs.e[i];
        if (e->used && e->count >= 2) {
            rc = heap_push(&tr.heap, e->count, e->a, e->b);
            if (rc != GT_OK) goto fail;
        }
    }

    /* Merge loop. */
    gt_learned_merge *merges = NULL;
    size_t nmerges = 0, capmerges = 0;
    while (tr.next_id < vocab_size) {
        uint32_t ba = 0, bb = 0;
        bool have = false;
        /* Pop until the top agrees with the live table (lazy deletion). */
        for (;;) {
            uint64_t c;
            uint32_t a, b;
            if (!heap_pop(&tr.heap, &c, &a, &b)) break;
            gt_pentry *e = ptab_get(&tr.pairs, a, b, false);
            if (!e || e->count != c || c < 2) continue;
            ba = a;
            bb = b;
            have = true;
            break;
        }
        if (!have) break;
        uint32_t nid = (uint32_t)tr.next_id++;
        /* Record the merge's byte text: left bytes + right bytes. */
        if (nmerges == capmerges) {
            size_t nc = capmerges ? capmerges * 2 : 256;
            gt_learned_merge *p = (gt_learned_merge *)realloc(merges, nc * sizeof(*p));
            if (!p) { rc = GT_ERR_OOM; goto fail_merges; }
            merges = p;
            capmerges = nc;
        }
        gt_learned_merge *m = &merges[nmerges++];
        gt_buf_init(&m->left);
        gt_buf_init(&m->right);
        m->id = nid;
        rc = gt_buf_append(&m->left, tr.symtext[ba].data, tr.symtext[ba].len);
        if (rc == GT_OK) rc = gt_buf_append(&m->right, tr.symtext[bb].data, tr.symtext[bb].len);
        if (rc != GT_OK) goto fail_merges;
        {
            gt_buf both;
            gt_buf_init(&both);
            rc = gt_buf_append(&both, m->left.data, m->left.len);
            if (rc == GT_OK) rc = gt_buf_append(&both, m->right.data, m->right.len);
            if (rc == GT_OK) rc = trainer_symtext(&tr, nid, both.data, both.len);
            gt_buf_free(&both);
            if (rc != GT_OK) goto fail_merges;
        }
        /* Apply to every word containing (ba, bb). Stale word-list entries
         * are harmless: a word with no live occurrence is scanned and left
         * alone. Counts are adjusted only for pairs actually removed/added. */
        gt_pentry *pe = ptab_get(&tr.pairs, ba, bb, false);
        if (pe) {
            size_t nw = pe->nwords;
            uint32_t *ws = pe->words;
            pe->nwords = 0;
            pe->count = 0;
            for (size_t k = 0; k < nw; k++) {
                uint32_t wi = ws[k];
                gt_word *w = &tr.words[wi];
                /* does this word still contain (ba,bb)? */
                size_t hits = 0;
                for (size_t q = 0; q + 1 < w->n; q++)
                    if (w->syms[q] == ba && w->syms[q + 1] == bb) hits++;
                if (hits == 0) continue;
                /* subtract this word's pairs, merge, re-add */
                for (size_t q = 0; q + 1 < w->n; q++) {
                    gt_pentry *e2 = ptab_get(&tr.pairs, w->syms[q], w->syms[q + 1], false);
                    if (e2 && e2->count >= w->count) e2->count -= w->count;
                    else if (e2) e2->count = 0;
                }
                /* leftmost non-overlapping merge, in place */
                size_t r = 0, q = 0;
                while (q < w->n) {
                    if (q + 1 < w->n && w->syms[q] == ba && w->syms[q + 1] == bb) {
                        w->syms[r++] = nid;
                        q += 2;
                    } else {
                        w->syms[r++] = w->syms[q++];
                    }
                }
                w->n = r;
                for (size_t q2 = 0; q2 + 1 < w->n; q2++) {
                    gt_pentry *e2 = ptab_get(&tr.pairs, w->syms[q2], w->syms[q2 + 1], true);
                    if (!e2) { rc = GT_ERR_OOM; goto fail_merges; }
                    e2->count += w->count;
                    rc = pentry_add_word(e2, wi);
                    if (rc != GT_OK) goto fail_merges;
                    rc = heap_push(&tr.heap, e2->count, e2->a, e2->b);
                    if (rc != GT_OK) goto fail_merges;
                }
            }
            free(ws);
            /* Re-fetch: nested inserts above may have grown (moved) the table. */
            pe = ptab_get(&tr.pairs, ba, bb, false);
            if (pe) {
                pe->words = NULL;
                pe->nwords = pe->capwords = 0;
            }
        }
        continue;
    fail_merges:
        for (size_t i = 0; i < nmerges; i++) {
            gt_buf_free(&merges[i].left);
            gt_buf_free(&merges[i].right);
        }
        free(merges);
        goto fail;
    }

    /* Emit vocabulary: 256 byte symbols, then merges in order, then specials. */
    {
        size_t ntok = tr.next_id + nspecial;
        gt_learned_token *toks = (gt_learned_token *)calloc(ntok ? ntok : 1, sizeof(*toks));
        if (!toks) { rc = GT_ERR_OOM; goto fail2; }
        for (uint32_t id = 0; id < tr.next_id; id++) {
            toks[id].id = id;
            gt_buf_init(&toks[id].bytes);
            rc = gt_buf_append(&toks[id].bytes, tr.symtext[id].data, tr.symtext[id].len);
            if (rc != GT_OK) {
                for (uint32_t j = 0; j < id; j++) gt_buf_free(&toks[j].bytes);
                free(toks);
                goto fail2;
            }
        }
        for (size_t i = 0; i < nspecial; i++) {
            uint32_t id = (uint32_t)(tr.next_id + i);
            toks[id].id = id;
            gt_buf_init(&toks[id].bytes);
            rc = gt_buf_append(&toks[id].bytes, specials[i].ptr, specials[i].len);
            if (rc != GT_OK) {
                for (uint32_t j = 0; j < id; j++) gt_buf_free(&toks[j].bytes);
                free(toks);
                goto fail2;
            }
        }
        out->toks = toks;
        out->ntoks = ntok;
        out->merges = merges;
        out->nmerges = nmerges;
    }
fail2:
    for (size_t i = 0; i < tr.nwords; i++) word_free(&tr.words[i]);
    free(tr.words);
    ptab_free(&tr.pairs);
    free(tr.heap.e);
    for (size_t i = 0; i < tr.nsymtext; i++) gt_buf_free(&tr.symtext[i]);
    free(tr.symtext);
    return rc;
fail:
    for (size_t i = 0; i < tr.nwords; i++) word_free(&tr.words[i]);
    free(tr.words);
    ptab_free(&tr.pairs);
    free(tr.heap.e);
    for (size_t i = 0; i < tr.nsymtext; i++) gt_buf_free(&tr.symtext[i]);
    free(tr.symtext);
    return rc;
}

/* ---- JSON writer -------------------------------------------------------- */

static gt_status json_escape(gt_buf *out, const uint8_t *p, size_t n) {
    gt_status rc = gt_buf_push_byte(out, '"');
    for (size_t i = 0; i < n && rc == GT_OK; i++) {
        uint8_t c = p[i];
        if (c == '"' || c == '\\') {
            rc = gt_buf_push_byte(out, '\\');
            if (rc == GT_OK) rc = gt_buf_push_byte(out, c);
        } else if (c < 0x20) {
            char tmp[8];
            int m = snprintf(tmp, sizeof tmp, "\\u%04x", c);
            (void)m;
            rc = gt_buf_append(out, tmp, 6);
        } else {
            rc = gt_buf_push_byte(out, c);
        }
    }
    if (rc == GT_OK) rc = gt_buf_push_byte(out, '"');
    return rc;
}

gt_status gt_train_write_json(const gt_trained *t, const char *path) {
    if (!t || !path) return GT_ERR_NULL_ARG;
    /* Map every vocab byte string through the byte alphabet so keys are the
     * byte-mapped text the loader (and the BPE stage) operates on. */
    gt_buf out;
    gt_buf_init(&out);
    static const char head[] =
        "{\"version\":\"1.0\",\"truncation\":null,"
        "\"padding\":null,\"added_tokens\":[],\"normalizer\":null,"
        "\"pre_tokenizer\":{\"type\":\"ByteLevel\","
        "\"add_prefix_space\":false,\"trim_offsets\":true},"
        "\"post_processor\":null,\"decoder\":null,"
        "\"model\":{\"type\":\"BPE\",\"dropout\":null,"
        "\"unk_token\":null,\"continuing_subword_prefix\":null,"
        "\"end_of_word_suffix\":null,\"fuse_unk\":false,\"vocab\":{";
    gt_status rc = gt_buf_append(&out, head, strlen(head));
    for (size_t i = 0; i < t->ntoks && rc == GT_OK; i++) {
        /* byte-map the token bytes into text */
        gt_buf key;
        gt_buf_init(&key);
        for (size_t k = 0; k < t->toks[i].bytes.len && rc == GT_OK; k++) {
            uint32_t cp = gt_byte_to_cp(t->toks[i].bytes.data[k]);
            uint8_t tmp[4];
            size_t tl = 0;
            if (cp < 0x80) tmp[tl++] = (uint8_t)cp;
            else if (cp < 0x800) {
                tmp[tl++] = (uint8_t)(0xC0 | (cp >> 6));
                tmp[tl++] = (uint8_t)(0x80 | (cp & 0x3F));
            } else {
                tmp[tl++] = (uint8_t)(0xE0 | (cp >> 12));
                tmp[tl++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                tmp[tl++] = (uint8_t)(0x80 | (cp & 0x3F));
            }
            rc = gt_buf_append(&key, tmp, tl);
        }
        if (rc == GT_OK) rc = json_escape(&out, key.data, key.len);
        gt_buf_free(&key);
        if (rc == GT_OK) {
            char tmp[32];
            int m = snprintf(tmp, sizeof tmp, ":%u%s", t->toks[i].id,
                             i + 1 < t->ntoks ? "," : "");
            (void)m;
            rc = gt_buf_append(&out, tmp, strlen(tmp));
        }
    }
    if (rc == GT_OK)
        rc = gt_buf_append(&out, "},\"merges\":[", 12);
    for (size_t i = 0; i < t->nmerges && rc == GT_OK; i++) {
        /* merges are "left right" in byte-mapped text */
        gt_buf lr;
        gt_buf_init(&lr);
        const gt_buf *sides[2] = {&t->merges[i].left, &t->merges[i].right};
        for (int s2 = 0; s2 < 2 && rc == GT_OK; s2++) {
            for (size_t k = 0; k < sides[s2]->len && rc == GT_OK; k++) {
                uint32_t cp = gt_byte_to_cp(sides[s2]->data[k]);
                uint8_t tmp[4];
                size_t tl = 0;
                if (cp < 0x80) tmp[tl++] = (uint8_t)cp;
                else if (cp < 0x800) {
                    tmp[tl++] = (uint8_t)(0xC0 | (cp >> 6));
                    tmp[tl++] = (uint8_t)(0x80 | (cp & 0x3F));
                } else {
                    tmp[tl++] = (uint8_t)(0xE0 | (cp >> 12));
                    tmp[tl++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                    tmp[tl++] = (uint8_t)(0x80 | (cp & 0x3F));
                }
                rc = gt_buf_append(&lr, tmp, tl);
            }
            if (rc == GT_OK && s2 == 0) rc = gt_buf_push_byte(&lr, ' ');
        }
        if (rc == GT_OK) rc = json_escape(&out, lr.data, lr.len);
        gt_buf_free(&lr);
        if (rc == GT_OK && i + 1 < t->nmerges) rc = gt_buf_push_byte(&out, ',');
    }
    if (rc == GT_OK) rc = gt_buf_append(&out, "]}}", 3);
    if (rc == GT_OK) {
        FILE *f = fopen(path, "wb");
        if (!f) rc = GT_ERR_IO;
        else {
            if (out.len && fwrite(out.data, 1, out.len, f) != out.len) rc = GT_ERR_IO;
            fclose(f);
        }
    }
    gt_buf_free(&out);
    return rc;
}
