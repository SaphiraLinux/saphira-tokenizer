/* gt_train_bin.c — train a BPE tokenizer from a corpus file, in C.
 *
 * Usage: gt_train <corpus> <vocab_size> <out.json>
 *   corpus: raw bytes; documents are split on \n (newlines not encoded)
 *
 * No Python. The output is a GPT-2-style tokenizer.json that gt_dump_ids
 * and gt_tokenizer_load read back directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_common.h"
#include "gt_train.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: gt_train <corpus> <vocab_size> <out.json>\n");
        return 2;
    }
    gt_buf corpus;
    gt_buf_init(&corpus);
    if (gt_read_file(argv[1], &corpus) != GT_OK) {
        fprintf(stderr, "cannot read corpus\n");
        return 1;
    }
    size_t vocab_size = (size_t)atol(argv[2]);
    if (vocab_size < 257) {
        fprintf(stderr, "vocab_size must be >= 257 (256 bytes + a merge)\n");
        return 2;
    }
    size_t ndocs = 1;
    for (size_t i = 0; i < corpus.len; i++)
        if (corpus.data[i] == '\n') ndocs++;
    gt_bytes *docs = malloc(ndocs * sizeof(*docs));
    size_t nd = 0, start = 0;
    for (size_t i = 0; i <= corpus.len; i++) {
        if (i == corpus.len || corpus.data[i] == '\n') {
            docs[nd].ptr = corpus.data + start;
            docs[nd].len = i - start;
            nd++;
            start = i + 1;
        }
    }
    gt_trained t;
    memset(&t, 0, sizeof t);
    gt_status rc = gt_train(docs, nd, vocab_size, NULL, 0, &t);
    if (rc != GT_OK) {
        fprintf(stderr, "train failed: %s\n", gt_status_str(rc));
        return 1;
    }
    printf("learned: tokens=%zu merges=%zu\n", t.ntoks, t.nmerges);
    rc = gt_train_write_json(&t, argv[3]);
    if (rc != GT_OK) {
        fprintf(stderr, "write failed: %s\n", gt_status_str(rc));
        return 1;
    }
    printf("wrote %s\n", argv[3]);
    gt_trained_free(&t);
    free(docs);
    gt_buf_free(&corpus);
    return 0;
}
