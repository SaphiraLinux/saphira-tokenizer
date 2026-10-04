/* mt_demo.c — share one tokenizer across threads the right way.
 *
 * Build from src/:  make examples
 * Run:  ./build/mt_demo <tokenizer.json> <threads> <text> [text...]
 *
 * Each thread encodes every input with gt_tokenizer_encode_mt on the SAME
 * read-only tokenizer and folds the ids into a checksum. All threads must
 * print the same checksum; if they don't, sharing is broken.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_tokenizer.h"

typedef struct {
    const gt_tokenizer *tk;
    char **texts;
    int ntexts;
    unsigned long long sum;
} job_t;

static void *run(void *arg) {
    job_t *j = (job_t *)arg;
    gt_ids ids;
    gt_ids_init(&ids);
    unsigned long long sum = 0;
    for (int i = 0; i < j->ntexts; i++) {
        gt_ids_clear(&ids);
        gt_bytes in = {(const uint8_t *)j->texts[i], strlen(j->texts[i])};
        gt_encode_stats st;
        if (gt_tokenizer_encode_mt(j->tk, in, &ids, &st) != GT_OK) {
            fprintf(stderr, "thread encode failed\n");
            break;
        }
        for (size_t k = 0; k < ids.len; k++) sum += ids.data[k];
    }
    gt_ids_free(&ids);
    j->sum = sum;
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: mt_demo <tokenizer.json> <threads> <text> [text...]\n");
        return 2;
    }
    gt_tokenizer *tk = NULL;
    if (gt_tokenizer_load(argv[1], &tk) != GT_OK) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    int nt = atoi(argv[2]);
    if (nt < 1) nt = 1;
    job_t *jobs = calloc((size_t)nt, sizeof(*jobs));
    pthread_t *ths = malloc((size_t)nt * sizeof(*ths));
    for (int t = 0; t < nt; t++) {
        jobs[t].tk = tk;
        jobs[t].texts = &argv[3];
        jobs[t].ntexts = argc - 3;
        pthread_create(&ths[t], NULL, run, &jobs[t]);
    }
    int ok = 1;
    for (int t = 0; t < nt; t++) {
        pthread_join(ths[t], NULL);
        printf("thread %d checksum=%llu\n", t, jobs[t].sum);
        if (t > 0 && jobs[t].sum != jobs[0].sum) ok = 0;
    }
    printf("%s\n", ok ? "AGREE" : "DISAGREE");
    free(jobs);
    free(ths);
    gt_tokenizer_free(tk);
    return ok ? 0 : 1;
}
