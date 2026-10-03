/* gt_debug_stages.c — run one document through the stages and print each one.
 *
 * This is the localization tool: when the differential harness reports a
 * mismatch, this says which stage produced the wrong thing. It uses only the
 * public per-stage API, so what it prints is what the library actually does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_bpe.h"
#include "gt_bytemap.h"
#include "gt_json.h"
#include "gt_pretok.h"
#include "gt_tokenizer.h"
#include "gt_unicode.h"
#include "gt_vocab.h"

static void put_hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static void put_cps(const uint16_t *p, size_t n) {
    putchar('[');
    for (size_t i = 0; i < n; i++) printf("%s%u", i ? " " : "", p[i]);
    putchar(']');
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: gt_debug_stages <tokenizer.json> <hex>\n"); return 2; }
    gt_tokenizer *tk = NULL;
    gt_status rc = gt_tokenizer_load(argv[1], &tk);
    if (rc != GT_OK) { fprintf(stderr, "load: %s\n", gt_status_str(rc)); return 1; }

    gt_buf doc;
    gt_buf_init(&doc);
    if (argc > 2) {
        const char *h = argv[2];
        int hi = -1;
        for (const char *p = h; *p; p++) {
            int v;
            if (*p >= '0' && *p <= '9') v = *p - '0';
            else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
            else continue;
            if (hi < 0) hi = v;
            else { gt_buf_push_byte(&doc, (uint8_t)((hi << 4) | v)); hi = -1; }
        }
    }

    /* Reach the stages through the tokenizer's own tables so this reports
     * real behaviour, not a re-implementation. */
    extern const gt_bpe *gt_debug_bpe(const gt_tokenizer *);
    extern const gt_vocab *gt_debug_vocab(const gt_tokenizer *);
    const gt_bpe *bpe = gt_debug_bpe(tk);
    const gt_vocab *vocab = gt_debug_vocab(tk);

    printf("document: "); put_hex(doc.data, doc.len); putchar('\n');

    gt_pretok_iter it;
    gt_pretok_iter_init(&it, (gt_bytes){doc.data, doc.len});
    gt_bytes piece;
    size_t pnum = 0, total_ids = 0;
    while (gt_pretok_next(&it, &piece)) {
        printf("pretoken[%zu]: ", pnum++);
        put_hex(piece.ptr, piece.len);
        putchar(' ');

        size_t n = piece.len ? piece.len : 1;
        uint16_t *cps = (uint16_t *)malloc(n * sizeof(uint16_t));
        for (size_t i = 0; i < piece.len; i++) cps[i] = (uint16_t)gt_byte_to_cp(piece.ptr[i]);
        printf(" cps="); put_cps(cps, piece.len);

        gt_symbol *syms = (gt_symbol *)malloc(n * sizeof(gt_symbol));
        size_t ns = gt_bpe_merge(bpe, cps, piece.len, syms, n);
        printf(" syms=%zu[", ns);
        for (size_t i = 0; i < ns; i++) {
            printf("%s(off=%u len=%u:", i ? " " : "", syms[i].off, syms[i].len);
            put_cps(cps + syms[i].off, syms[i].len);
            gt_token_id id = gt_vocab_lookup(vocab, cps + syms[i].off, syms[i].len);
            printf("->%s", id == GT_ID_NONE ? "MISS" : "");
            if (id != GT_ID_NONE) printf("%u", id);
            printf(")");
        }
        printf("]");
        for (size_t i = 0; i < ns; i++)
            if (gt_vocab_lookup(vocab, cps + syms[i].off, syms[i].len) == GT_ID_NONE)
                printf("\n  *** VOCAB MISS at symbol %zu ***", i);
        putchar('\n');
        total_ids += ns;
        free(cps);
        free(syms);
    }
    printf("pretokens: %zu  symbols: %zu\n", pnum, total_ids);
    gt_buf_free(&doc);
    gt_tokenizer_free(tk);
    return 0;
}
