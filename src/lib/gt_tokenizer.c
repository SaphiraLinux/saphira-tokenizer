/* gt_tokenizer.c — load tokenizer.json, and run the stages in order. */
#include "gt_tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_bytemap.h"
#include "gt_json.h"
#include "gt_pretok.h"
#include "gt_unicode.h"

/* Pretoken memo, v2: DIRECT-MAPPED, not open-addressed.
 *
 * v1 failed (7x slowdown) for three concrete reasons this fixes:
 *  1. two hashes per pretoken (lookup, then insert) -> one hash, reused;
 *  2. unbounded linear probes on an 8k table that filled up -> exactly one
 *     slot per pretoken (index = hash & mask), overwrite on collision;
 *  3. 256KB table thrashing L1/L2 -> 2k slots (~64KB) so hot entries stay put.
 * A collision only evicts (never corrupts): memcmp verifies every hit, so a
 * hit returns bit-identical ids to a miss. Pure memoisation.
 */
#define GT_PC_BITS 11
#define GT_PC_SIZE (1u << GT_PC_BITS)
#define GT_PC_MASK (GT_PC_SIZE - 1)
#define GT_PC_MAXKEY 15
#define GT_PC_MAXIDS 7

typedef struct {
    /* Seqlock counter: even = stable, odd = writer inside. ONLY touched via
     * __atomic_* builtins (plain uint32_t so those builtins accept it); see
     * the lookup/insert below for the protocol. */
    uint32_t seq;
    uint8_t klen;
    uint8_t key[GT_PC_MAXKEY];
    uint8_t nids;
    gt_token_id ids[GT_PC_MAXIDS];
} gt_pcent;

struct gt_tokenizer {
    gt_vocab *vocab;
    gt_bpe *bpe;
    gt_special *special;
    char pretok_kind[32];
    gt_encode_stats stats;
    gt_pcent *pcache;
    /* Striped insert spinlocks: concurrent inserts to the same slot would
     * break the seqlock (two writers interleave 0->1->2->3->4 and land even
     * with torn data). One flag per 8 slots; only inserts take them, so the
     * hot lookup path stays lock-free. */
    char *ilock;
};

static uint32_t pchash(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= (uint32_t)n;
    h *= 16777619u;
    return h;
}

/* ---- loading ---------------------------------------------------------- */

/* Decode UTF-8 text into mapped codepoints, rejecting anything outside the
 * alphabet: a vocab key that is not byte-mapped would mean this is not a
 * byte-level BPE tokenizer. */
static gt_status text_to_mapped(const gt_buf *text, uint16_t **out, size_t *out_n) {
    size_t cap = text->len ? text->len : 1, n = 0;
    uint16_t *cps = (uint16_t *)malloc(cap * sizeof(uint16_t));
    if (!cps) return GT_ERR_OOM;
    size_t i = 0;
    while (i < text->len) {
        size_t q = i;
        uint32_t cp;
        gt_cp_class cls = gt_decode_next(text->data, text->len, &q, &cp);
        if (cls == GT_CLS_INVALID_UTF8) { free(cps); return GT_ERR_BAD_FORMAT; }
        if (n == cap) {
            size_t nc = cap * 2;
            uint16_t *p = (uint16_t *)realloc(cps, nc * sizeof(uint16_t));
            if (!p) { free(cps); return GT_ERR_OOM; }
            cps = p; cap = nc;
        }
        cps[n++] = (uint16_t)cp;
        i = q;
    }
    *out = cps;
    *out_n = n;
    return GT_OK;
}

/* Growable pointer arrays for the three tables tokenizer.json supplies. */
typedef struct {
    uint16_t *cps;      /* all keys concatenated, entry order */
    size_t *lens;       /* per entry, in codepoints */
    gt_token_id *ids;   /* per entry */
    size_t n, cap;      /* entry count and entry capacity */
    size_t total;       /* codepoints written into cps */
    size_t cps_cap;     /* cps capacity, in codepoints */
} vkey_tab;

typedef struct {
    char **rule;
    size_t n, cap;
} str_tab;

typedef struct {
    gt_added_token *tok;
    size_t n, cap;
} add_tab;

static gt_status vkey_push(vkey_tab *v, const uint16_t *cps, size_t n, gt_token_id id) {
    /* Entry arrays grow by entry count, the flat key storage by CODEPOINT
     * count: one entry may hold several codepoints, so a single entry capacity
     * cannot size both. */
    if (v->n == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 1024;
        size_t *a2 = (size_t *)realloc(v->lens, nc * sizeof(size_t));
        if (!a2) return GT_ERR_OOM;
        v->lens = a2;
        gt_token_id *a3 = (gt_token_id *)realloc(v->ids, nc * sizeof(gt_token_id));
        if (!a3) return GT_ERR_OOM;
        v->ids = a3;
        v->cap = nc;
    }
    if (v->total + n > v->cps_cap) {
        size_t nc = v->cps_cap ? v->cps_cap * 2 : 4096;
        while (nc < v->total + n) nc *= 2;
        uint16_t *a1 = (uint16_t *)realloc(v->cps, nc * sizeof(uint16_t));
        if (!a1) return GT_ERR_OOM;
        v->cps = a1;
        v->cps_cap = nc;
    }
    if (n) memcpy(v->cps + v->total, cps, n * sizeof(uint16_t));
    v->total += n;
    v->lens[v->n] = n;
    v->ids[v->n] = id;
    v->n++;
    return GT_OK;
}

static gt_status str_push(str_tab *t, const gt_buf *b) {
    if (t->n == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 1024;
        char **p = (char **)realloc(t->rule, nc * sizeof(char *));
        if (!p) return GT_ERR_OOM;
        t->rule = p;
        t->cap = nc;
    }
    char *copy = (char *)malloc(b->len + 1);
    if (!copy) return GT_ERR_OOM;
    if (b->len) memcpy(copy, b->data, b->len);
    copy[b->len] = 0;
    t->rule[t->n++] = copy;
    return GT_OK;
}

static gt_status add_push(add_tab *t, const gt_added_token *a) {
    if (t->n == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 8;
        gt_added_token *p = (gt_added_token *)realloc(t->tok, nc * sizeof(*p));
        if (!p) return GT_ERR_OOM;
        t->tok = p;
        t->cap = nc;
    }
    t->tok[t->n++] = *a;
    return GT_OK;
}

static void vkey_free(vkey_tab *v) { free(v->cps); free(v->lens); free(v->ids); }
static void str_free(str_tab *t) { for (size_t i = 0; i < t->n; i++) free(t->rule[i]); free(t->rule); }
static void add_free(add_tab *t) { free(t->tok); }

/* Parse one "model" object: vocab map + merges list. */
static gt_status parse_model(gt_json *j, vkey_tab *vocab, str_tab *merges) {
    gt_buf key, text;
    gt_status rc;
    gt_buf_init(&key);
    gt_buf_init(&text);

    if (!gt_eat(j, '{')) { rc = GT_ERR_BAD_FORMAT; goto out; }
    bool md = false;
    while (!md) {
        rc = gt_json_object_key(j, &key);
        if (rc != GT_OK) goto out;
        if (gt_buf_eq(&key, "vocab")) {
            if (!gt_eat(j, '{')) { rc = GT_ERR_BAD_FORMAT; goto out; }
            bool vd = false;
            while (!vd) {
                rc = gt_json_string(j, &text);
                if (rc != GT_OK) goto out;
                if (!gt_eat(j, ':')) { rc = GT_ERR_BAD_FORMAT; goto out; }
                int64_t idv;
                rc = gt_json_number_i64(j, &idv);
                if (rc != GT_OK) goto out;
                uint16_t *cps = NULL;
                size_t cn = 0;
                rc = text_to_mapped(&text, &cps, &cn);
                if (rc != GT_OK) goto out;
                rc = vkey_push(vocab, cps, cn, (gt_token_id)idv);
                free(cps);
                if (rc != GT_OK) goto out;
                rc = gt_json_obj_next(j, &vd);
                if (rc != GT_OK) goto out;
            }
        } else if (gt_buf_eq(&key, "merges")) {
            /* The array is homogeneous: either ["a b", "c d"] (older) or
             * [["a","b"], ["c","d"]] (newer). Decide ONCE from the first
             * element, because a string rule followed by a comma is otherwise
             * indistinguishable from the start of a pair. */
            bool pairs;
            if (!gt_eat(j, '[')) { rc = GT_ERR_BAD_FORMAT; goto out; }
            pairs = (gt_peek(j) == '[');
            bool gd = false;
            while (!gd) {
                if (pairs) {
                    if (!gt_eat(j, '[')) { rc = GT_ERR_BAD_FORMAT; goto out; }
                    gt_buf a, b;
                    gt_buf_init(&a);
                    gt_buf_init(&b);
                    rc = gt_json_string(j, &a);
                    if (rc == GT_OK && !gt_eat(j, ',')) rc = GT_ERR_BAD_FORMAT;
                    if (rc == GT_OK) rc = gt_json_string(j, &b);
                    if (rc == GT_OK && !gt_eat(j, ']')) rc = GT_ERR_BAD_FORMAT;
                    if (rc == GT_OK) {
                        gt_buf rule;
                        gt_buf_init(&rule);
                        rc = gt_buf_append(&rule, a.data, a.len);
                        if (rc == GT_OK) rc = gt_buf_push_byte(&rule, ' ');
                        if (rc == GT_OK) rc = gt_buf_append(&rule, b.data, b.len);
                        if (rc == GT_OK) rc = str_push(merges, &rule);
                        gt_buf_free(&rule);
                    }
                    gt_buf_free(&a);
                    gt_buf_free(&b);
                    if (rc != GT_OK) goto out;
                } else {
                    rc = gt_json_string(j, &text);
                    if (rc != GT_OK) goto out;
                    rc = str_push(merges, &text);
                    if (rc != GT_OK) goto out;
                }
                rc = gt_json_arr_next(j, &gd);
                if (rc != GT_OK) goto out;
            }
        } else {
            rc = gt_json_skip_value(j);
            if (rc != GT_OK) goto out;
        }
        rc = gt_json_obj_next(j, &md);
        if (rc != GT_OK) goto out;
    }
    rc = GT_OK;
out:
    gt_buf_free(&key);
    gt_buf_free(&text);
    return rc;
}

/* Parse "added_tokens": [{id, content, special}, ...] */
static gt_status parse_added(gt_json *j, add_tab *added) {
    gt_buf key;
    gt_status rc;
    gt_buf_init(&key);
    bool adone = false;
    gt_json j2 = *j;
    rc = gt_json_arr_begin(&j2, &adone);
    if (rc != GT_OK) { gt_buf_free(&key); return rc; }

    while (!adone) {
        gt_added_token a;
        memset(&a, 0, sizeof a);
        a.special = false;
        if (!gt_eat(&j2, '{')) { rc = GT_ERR_BAD_FORMAT; goto out; }
        bool ad = false;
        gt_buf content;
        gt_buf_init(&content);
        while (!ad) {
            rc = gt_json_object_key(&j2, &key);
            if (rc != GT_OK) { gt_buf_free(&content); goto out; }
            if (gt_buf_eq(&key, "id")) {
                int64_t v;
                rc = gt_json_number_i64(&j2, &v);
                if (rc == GT_OK) a.id = (gt_token_id)v;
            } else if (gt_buf_eq(&key, "content")) {
                rc = gt_json_string(&j2, &content);
            } else if (gt_buf_eq(&key, "special")) {
                if (gt_lit(&j2, "true")) a.special = true;
                else if (!gt_lit(&j2, "false")) rc = GT_ERR_BAD_FORMAT;
            } else {
                rc = gt_json_skip_value(&j2);
            }
            if (rc != GT_OK) { gt_buf_free(&content); goto out; }
            rc = gt_json_obj_next(&j2, &ad);
            if (rc != GT_OK) { gt_buf_free(&content); goto out; }
        }
        a.content.ptr = content.data;
        a.content.len = content.len;
        rc = add_push(added, &a);   /* owns the buffer via ptr */
        gt_buf_clear(&content);      /* buffer freed below, data kept */
        gt_buf_free(&content);
        if (rc != GT_OK) goto out;
        rc = gt_json_arr_next(&j2, &adone);
        if (rc != GT_OK) goto out;
    }
    *j = j2;
    rc = GT_OK;
out:
    gt_buf_free(&key);
    return rc;
}

gt_status gt_tokenizer_load(const char *path, gt_tokenizer **out) {
    if (!path || !out) return GT_ERR_NULL_ARG;
    *out = NULL;

    gt_buf file;
    gt_buf_init(&file);
    gt_status rc = gt_read_file(path, &file);
    if (rc != GT_OK) { gt_buf_free(&file); return rc; }

    gt_tokenizer *t = (gt_tokenizer *)calloc(1, sizeof(*t));
    if (!t) { gt_buf_free(&file); return GT_ERR_OOM; }
    t->pretok_kind[0] = 0;

    gt_json j;
    gt_json_init(&j, (const char *)(file.data ? (const char *)file.data : ""), file.len);

    vkey_tab vocab = {0};
    str_tab merges = {0};
    add_tab added = {0};
    bool saw_model = false;

    if (!gt_eat(&j, '{')) { rc = GT_ERR_BAD_FORMAT; goto fail; }
    bool done = false;
    gt_buf key;
    gt_buf_init(&key);
    while (!done) {
        rc = gt_json_object_key(&j, &key);
        if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
        if (gt_buf_eq(&key, "pre_tokenizer")) {
            if (!gt_eat(&j, '{')) { rc = GT_ERR_BAD_FORMAT; gt_buf_free(&key); goto fail; }
            bool pd = false;
            while (!pd) {
                rc = gt_json_object_key(&j, &key);
                if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
                if (gt_buf_eq(&key, "type")) {
                    gt_buf v;
                    gt_buf_init(&v);
                    rc = gt_json_string(&j, &v);
                    if (rc == GT_OK) {
                        size_t n = v.len < sizeof(t->pretok_kind) - 1 ? v.len
                                                                    : sizeof(t->pretok_kind) - 1;
                        memcpy(t->pretok_kind, v.data, n);
                        t->pretok_kind[n] = 0;
                    }
                    gt_buf_free(&v);
                    if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
                } else {
                    rc = gt_json_skip_value(&j);
                    if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
                }
                rc = gt_json_obj_next(&j, &pd);
                if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
            }
        } else if (gt_buf_eq(&key, "added_tokens")) {
            rc = parse_added(&j, &added);
            if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
        } else if (gt_buf_eq(&key, "model")) {
            rc = parse_model(&j, &vocab, &merges);
            if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
            saw_model = true;
        } else {
            rc = gt_json_skip_value(&j);
            if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
        }
        gt_buf_clear(&key);
        rc = gt_json_obj_next(&j, &done);
        if (rc != GT_OK) { gt_buf_free(&key); goto fail; }
    }
    gt_buf_free(&key);

    /* Validate what we found. Refusing loudly here is the whole point: a
     * normalizer, a different pre-tokenizer, or a missing model would all
     * silently produce wrong ids otherwise. */
    if (!saw_model) { rc = GT_ERR_MISSING_DATA; goto fail; }
    if (vocab.n == 0) { rc = GT_ERR_MISSING_DATA; goto fail; }
    if (strcmp(t->pretok_kind, "ByteLevel") != 0) { rc = GT_ERR_UNSUPPORTED; goto fail; }

    rc = gt_vocab_new(vocab.cps, vocab.lens, vocab.ids, vocab.n, &t->vocab);
    if (rc != GT_OK) goto fail;
    rc = gt_bpe_new((const char *const *)merges.rule, merges.n, &t->bpe);
    if (rc != GT_OK) goto fail;
    rc = gt_special_new(added.tok, added.n, &t->special);
    if (rc != GT_OK) goto fail;
    t->pcache = (gt_pcent *)calloc(GT_PC_SIZE, sizeof(gt_pcent));
    if (!t->pcache) { rc = GT_ERR_OOM; goto fail; }
    t->ilock = (char *)calloc(GT_PC_SIZE / 8, 1);
    if (!t->ilock) { rc = GT_ERR_OOM; goto fail; }

    vkey_free(&vocab);
    str_free(&merges);
    add_free(&added);
    gt_buf_free(&file);
    *out = t;
    return GT_OK;

fail:
    vkey_free(&vocab);
    str_free(&merges);
    add_free(&added);
    gt_tokenizer_free(t);
    gt_buf_free(&file);
    return rc;
}

void gt_tokenizer_free(gt_tokenizer *t) {
    if (!t) return;
    gt_vocab_free(t->vocab);
    gt_bpe_free(t->bpe);
    gt_special_free(t->special);
    free(t->pcache);
    free(t->ilock);
    free(t);
}

size_t gt_tokenizer_vocab_size(const gt_tokenizer *t) { return t ? gt_vocab_size(t->vocab) : 0; }
gt_token_id gt_tokenizer_max_id(const gt_tokenizer *t) { return t ? gt_vocab_max_id(t->vocab) : 0; }
size_t gt_tokenizer_merge_rules(const gt_tokenizer *t) { return t ? gt_bpe_rule_count(t->bpe) : 0; }
size_t gt_tokenizer_added_tokens(const gt_tokenizer *t) { return t ? gt_special_count(t->special) : 0; }
const char *gt_tokenizer_pretokenizer(const gt_tokenizer *t) { return t ? t->pretok_kind : ""; }

/* ---- encoding --------------------------------------------------------- */

/* Scratch sized from the input, reused across pretokens within one encode.
 * Sized from the largest pretoken, which is bounded by the input length. */
typedef struct {
    uint16_t *cps;
    size_t cap;
    gt_symbol *syms;
    size_t sym_cap;
} gt_scratch;

static gt_status scratch_reserve(gt_scratch *s, size_t need_cps, size_t need_syms) {
    if (need_cps > s->cap) {
        uint16_t *p = (uint16_t *)realloc(s->cps, need_cps * sizeof(uint16_t));
        if (!p) return GT_ERR_OOM;
        s->cps = p; s->cap = need_cps;
    }
    if (need_syms > s->sym_cap) {
        gt_symbol *p = (gt_symbol *)realloc(s->syms, need_syms * sizeof(gt_symbol));
        if (!p) return GT_ERR_OOM;
        s->syms = p; s->sym_cap = need_syms;
    }
    return GT_OK;
}

static void scratch_free(gt_scratch *s) {
    free(s->cps); free(s->syms);
    s->cps = NULL; s->syms = NULL;
    s->cap = s->sym_cap = 0;
}

static gt_status encode_inner(const gt_tokenizer *t, gt_bytes input, gt_ids *out,
                            gt_encode_stats *st) {
    if (!t || !out) return GT_ERR_NULL_ARG;
    gt_encode_stats local;
    if (!st) st = &local;
    memset(st, 0, sizeof *st);

    gt_status rc = gt_ids_reserve(out, input.len ? input.len : 1);
    if (rc != GT_OK) return rc;

    gt_pretok_iter it;
    gt_pretok_iter_init(&it, input);

    gt_scratch sc = {NULL, 0, NULL, 0};
    gt_bytes piece;
    while (gt_pretok_next(&it, &piece)) {
        st->n_pretokens++;

        /* Memo: one hash, one slot. A hit skips BPE+vocab entirely; a miss
         * fills the same slot so there is never a second hash or probe. */
        gt_pcent *slot = NULL;
        uint32_t ph = 0;
        if (piece.len >= 1 && piece.len <= GT_PC_MAXKEY) {
            ph = pchash(piece.ptr, piece.len);
            slot = &t->pcache[ph & GT_PC_MASK];
            /* Seqlock read: sample seq, copy bounded data, re-sample. A
             * change means a torn read -> miss (costs BPE, never wrong ids).
             * nids is clamped BEFORE use: a torn count must not size a copy. */
            uint32_t s0 = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
            if ((s0 & 1u) == 0 && slot->klen == piece.len &&
                memcmp(slot->key, piece.ptr, piece.len) == 0) {
                size_t nn = slot->nids;
                if (nn >= 1 && nn <= GT_PC_MAXIDS) {
                    gt_token_id tmp[GT_PC_MAXIDS];
                    for (size_t k = 0; k < nn; k++) tmp[k] = slot->ids[k];
                    uint32_t s1 = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
                    if (s0 == s1) {
                        st->n_pcache_hits++;
                        for (size_t k = 0; k < nn; k++) {
                            rc = gt_ids_push(out, tmp[k]);
                            if (rc != GT_OK) { scratch_free(&sc); return rc; }
                        }
                        st->n_vocab_hits += nn;
                        continue;
                    }
                }
            }
            st->n_pcache_miss++;
        }

        /* Stage: byte map. Every byte of the pretoken becomes one mapped
         * codepoint. */
        rc = scratch_reserve(&sc, piece.len ? piece.len : 1, piece.len ? piece.len : 1);
        if (rc != GT_OK) { scratch_free(&sc); return rc; }
        size_t ncps = 0;
        for (size_t i = 0; i < piece.len; i++) {
            sc.cps[ncps++] = (uint16_t)gt_byte_to_cp(piece.ptr[i]);
        }
        st->n_mapped_codepoints += ncps;

        /* Stage: BPE. */
        size_t nsym = gt_bpe_merge(t->bpe, sc.cps, ncps, sc.syms, sc.sym_cap);
        st->n_merged_symbols += nsym;

        /* Stage: vocabulary. */
        for (size_t i = 0; i < nsym; i++) {
            gt_token_id id = gt_vocab_lookup(t->vocab, sc.cps + sc.syms[i].off,
                                             sc.syms[i].len);
            if (id == GT_ID_NONE) {
                st->n_vocab_misses++;
                scratch_free(&sc);
                return GT_ERR_STAGE_VOCAB;
            }
            st->n_vocab_hits++;
            rc = gt_ids_push(out, id);
            if (rc != GT_OK) { scratch_free(&sc); return rc; }
        }
        /* Fill the memo slot found above (same hash, no second probe walk).
         * Only when the result fits; anything else bypasses next time too. */
        if (slot && nsym >= 1 && nsym <= GT_PC_MAXIDS) {
            char *lk = &t->ilock[(slot - t->pcache) / 8];
            while (__atomic_test_and_set(lk, __ATOMIC_ACQUIRE)) { /* spin */ }
            __atomic_fetch_add(&slot->seq, 1u, __ATOMIC_RELAXED);
            slot->klen = (uint8_t)piece.len;
            memcpy(slot->key, piece.ptr, piece.len);
            slot->nids = (uint8_t)nsym;
            for (size_t k = 0; k < nsym; k++)
                slot->ids[k] = out->data[out->len - nsym + k];
            __atomic_fetch_add(&slot->seq, 1u, __ATOMIC_RELEASE);
            __atomic_clear(lk, __ATOMIC_RELEASE);
        }
    }
    scratch_free(&sc);
    return GT_OK;
}

gt_status gt_tokenizer_encode(gt_tokenizer *t, gt_bytes input, gt_ids *out) {
    if (!t) return GT_ERR_NULL_ARG;
    gt_status rc = encode_inner(t, input, out, &t->stats);
    return rc;
}

/* Thread-safe: shares the read-only tables, writes stats to the caller.
 * Concurrent calls on the same tokenizer are safe; concurrent calls sharing
 * an `out` or `st` are not (same rule as any reentrant C function). */
gt_status gt_tokenizer_encode_mt(const gt_tokenizer *t, gt_bytes input, gt_ids *out,
                                 gt_encode_stats *st) {
    return encode_inner(t, input, out, st);
}

/* Accessors for the stage debugger, so it exercises the tokenizer's own tables
 * instead of rebuilding them and reporting on a different program. */
const gt_bpe *gt_debug_bpe(const gt_tokenizer *t) { return t ? t->bpe : NULL; }
const gt_vocab *gt_debug_vocab(const gt_tokenizer *t) { return t ? t->vocab : NULL; }

void gt_tokenizer_encode_stats(const gt_tokenizer *t, gt_encode_stats *out) {
    if (t && out) *out = t->stats;
}
