/* gt_bench.c — throughput benchmark, not a correctness tool.
 *
 * Each worker thread owns a private tokenizer (the library stays
 * single-threaded and simple; documents are independent so sharing nothing
 * is the correct parallel structure). Reports input MB/s and a checksum over
 * all ids so a fast-but-wrong build cannot hide.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gt_tokenizer.h"

typedef struct {
    gt_tokenizer *tk;
    const uint8_t **docs;
    const size_t *lens;
    size_t start, end;
    uint64_t sum;
    size_t ntok;
    int rc;
} worker_t;

static void *worker(void *arg) {
    worker_t *w = (worker_t *)arg;
    gt_ids ids;
    gt_ids_init(&ids);
    uint64_t sum = 0;
    size_t ntok = 0;
    for (size_t i = w->start; i < w->end; i++) {
        gt_ids_clear(&ids);
        gt_bytes in = {w->docs[i], w->lens[i]};
        if (gt_tokenizer_encode(w->tk, in, &ids) != GT_OK) {
            w->rc = 1;
            break;
        }
        for (size_t k = 0; k < ids.len; k++) sum += ids.data[k];
        ntok += ids.len;
    }
    gt_ids_free(&ids);
    w->sum = sum;
    w->ntok = ntok;
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: gt_bench <tokenizer.json> <corpus.bin> <threads>\n"
                        "  corpus: raw bytes, split into docs on \\n (newlines not encoded)\n");
        return 2;
    }
    const char *tok_path = argv[1];
    int nthreads = atoi(argv[3]);
    if (nthreads < 1) nthreads = 1;

    gt_buf corpus;
    gt_buf_init(&corpus);
    if (gt_read_file(argv[2], &corpus) != GT_OK) {
        fprintf(stderr, "cannot read corpus\n");
        return 1;
    }
    size_t ndocs = 1;
    for (size_t i = 0; i < corpus.len; i++)
        if (corpus.data[i] == '\n') ndocs++;
    const uint8_t **docs = malloc(ndocs * sizeof(*docs));
    size_t *lens = malloc(ndocs * sizeof(*lens));
    size_t nd = 0, start = 0;
    for (size_t i = 0; i <= corpus.len; i++) {
        if (i == corpus.len || corpus.data[i] == '\n') {
            docs[nd] = corpus.data + start;
            lens[nd] = i - start;
            nd++;
            start = i + 1;
        }
    }
    size_t nbytes = 0;
    for (size_t i = 0; i < nd; i++) nbytes += lens[i];
    printf("docs=%zu inputMB=%.2f threads=%d\n", nd, nbytes / 1e6, nthreads);

    worker_t *ws = calloc((size_t)nthreads, sizeof(*ws));
    pthread_t *ths = malloc((size_t)nthreads * sizeof(*ths));
    /* One tokenizer per thread; load once (fast since the vocab sort fix). */
    gt_tokenizer *first = NULL;
    if (gt_tokenizer_load(tok_path, &first) != GT_OK) {
        fprintf(stderr, "cannot load tokenizer\n");
        return 1;
    }
    gt_tokenizer_free(first);
    for (int t = 0; t < nthreads; t++) {
        if (gt_tokenizer_load(tok_path, &ws[t].tk) != GT_OK) {
            fprintf(stderr, "thread %d: cannot load tokenizer\n", t);
            return 1;
        }
        ws[t].docs = docs;
        ws[t].lens = lens;
        ws[t].start = nd * (size_t)t / (size_t)nthreads;
        ws[t].end = nd * (size_t)(t + 1) / (size_t)nthreads;
    }
    double t0 = now();
    for (int t = 0; t < nthreads; t++)
        pthread_create(&ths[t], NULL, worker, &ws[t]);
    int rc = 0;
    uint64_t sum = 0;
    size_t ntok = 0;
    for (int t = 0; t < nthreads; t++) {
        pthread_join(ths[t], NULL);
        rc |= ws[t].rc;
        sum += ws[t].sum;
        ntok += ws[t].ntok;
        gt_tokenizer_free(ws[t].tk);
    }
    double dt = now() - t0;
    printf("rc=%d tokens=%zu checksum=%llu time=%.2fs -> %.2f MB/s input\n",
           rc, ntok, (unsigned long long)sum, dt, nbytes / 1e6 / dt);
    free(ws);
    free(ths);
    free(docs);
    free(lens);
    gt_buf_free(&corpus);
    return rc;
}
