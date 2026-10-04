/* gt_words.c — count distinct pretokens ("words") in a corpus.
 *
 * Usage: gt_words <corpus>  (documents split on \n)
 * Prints: total words, distinct words, singletons (count==1), and the
 * suggested vocab_size (= 256 byte symbols + distinct words).
 *
 * Uses the same pretokenizer training uses, so "word" here is exactly what
 * gt_train would deduplicate. Pure C, no Python.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_common.h"
#include "gt_pretok.h"

typedef struct {
    uint8_t *bytes;
    size_t len;
    uint64_t count;
    uint8_t used;
} went;

static went *tab;
static size_t tcap, tmask;

static uint64_t whash(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    h ^= n;
    h *= 1099511628211ull;
    return h;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: gt_words <corpus>\n");
        return 2;
    }
    gt_buf corpus;
    gt_buf_init(&corpus);
    if (gt_read_file(argv[1], &corpus) != GT_OK) return 1;

    tcap = 1 << 20;
    tmask = tcap - 1;
    tab = calloc(tcap, sizeof(*tab));
    if (!tab) return 1;

    uint64_t total = 0, distinct = 0;
    size_t start = 0;
    gt_pretok_iter it;
    gt_bytes piece;
    for (size_t i = 0; i <= corpus.len; i++) {
        if (i != corpus.len && corpus.data[i] != '\n') continue;
        gt_bytes doc = {corpus.data + start, i - start};
        start = i + 1;
        if (doc.len == 0) continue;
        gt_pretok_iter_init(&it, doc);
        while (gt_pretok_next(&it, &piece)) {
            if (piece.len == 0 || piece.len > 15) continue;
            total++;
            size_t s = (size_t)whash(piece.ptr, piece.len) & tmask;
            while (tab[s].used) {
                if (tab[s].len == piece.len &&
                    memcmp(tab[s].bytes, piece.ptr, piece.len) == 0) {
                    tab[s].count++;
                    break;
                }
                s = (s + 1) & tmask;
            }
            if (!tab[s].used) {
                tab[s].used = 1;
                tab[s].len = piece.len;
                tab[s].bytes = malloc(piece.len);
                memcpy(tab[s].bytes, piece.ptr, piece.len);
                tab[s].count = 1;
                distinct++;
            }
        }
    }
    uint64_t singletons = 0;
    for (size_t i = 0; i < tcap; i++)
        if (tab[i].used && tab[i].count == 1) singletons++;
    printf("total=%llu distinct=%llu singletons=%llu suggested_vocab=%llu\n",
           (unsigned long long)total, (unsigned long long)distinct,
           (unsigned long long)singletons, (unsigned long long)(256 + distinct));
    return 0;
}
