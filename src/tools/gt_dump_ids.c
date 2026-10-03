/* gt_dump_ids.c — encode documents from stdin, print token ids.
 *
 * Input is one document per line, HEX ENCODED. Hex rather than raw text
 * because the contract is arbitrary bytes: embedded NUL, invalid UTF-8, and
 * lone continuation bytes all have to survive the trip from the differential
 * harness into this process. A text-oriented CLI could not express them.
 *
 * Output is one line per document: space-separated ids, empty line for an
 * empty document.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_tokenizer.h"

static int hex_nibble(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode one hex line into `out`. Whitespace is skipped. An empty or
 * whitespace-only line is a legitimate empty document. */
static int hex_decode(const char *line, gt_buf *out) {
    gt_buf_clear(out);
    int hi = -1;
    for (const char *p = line; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int v = hex_nibble(c);
        if (v < 0) return -1;
        if (hi < 0) {
            hi = v;
        } else {
            gt_status rc = gt_buf_push_byte(out, (uint8_t)((hi << 4) | v));
            if (rc != GT_OK) return -1;
            hi = -1;
        }
    }
    return hi < 0 ? 0 : -1; /* odd digit count is a malformed line */
}

static void usage(void) {
    fprintf(stderr,
            "usage: gt_dump_ids <tokenizer.json> [--stats]\n"
            "  reads hex-encoded documents, one per line, on stdin\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    bool stats = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--stats") == 0) stats = true;
        else { usage(); return 2; }
    }

    gt_tokenizer *tk = NULL;
    gt_status rc = gt_tokenizer_load(argv[1], &tk);
    if (rc != GT_OK) {
        fprintf(stderr, "load failed: %s\n", gt_status_str(rc));
        return 1;
    }
    if (stats) {
        fprintf(stderr, "pretokenizer=%s vocab=%zu max_id=%u merges=%zu added=%zu\n",
                gt_tokenizer_pretokenizer(tk),
                gt_tokenizer_vocab_size(tk),
                gt_tokenizer_max_id(tk),
                gt_tokenizer_merge_rules(tk),
                gt_tokenizer_added_tokens(tk));
    }

    char *line = NULL;
    size_t linecap = 0;
    gt_buf doc;
    gt_buf_init(&doc);
    gt_ids ids;
    gt_ids_init(&ids);

    int status = 0;
    for (;;) {
        ssize_t n = getline(&line, &linecap, stdin);
        if (n < 0) break;
        if (n > 0 && line[n - 1] == '\n') line[--n] = 0;
        if (hex_decode(line, &doc) != 0) {
            fprintf(stderr, "bad hex on input line\n");
            status = 1;
            break;
        }
        gt_ids_clear(&ids);
        gt_bytes input = {doc.data, doc.len};
        rc = gt_tokenizer_encode(tk, input, &ids);
        if (rc != GT_OK) {
            /* Report and continue. Aborting would hide every later document,
             * so one divergence would mask the rest; the differential harness
             * needs the full picture to tell a single bad case from a
             * systematic one. */
            printf("ERR %d\n", (int)rc);
            status = 1;
            continue;
        }
        for (size_t i = 0; i < ids.len; i++) {
            if (i) putchar(' ');
            printf("%u", ids.data[i]);
        }
        putchar('\n');
    }
    if (stats) {
        gt_encode_stats st;
        gt_tokenizer_encode_stats(tk, &st);
        fprintf(stderr,
                "pretokens=%zu mapped_cps=%zu merged_symbols=%zu "
                "vocab_hits=%zu vocab_misses=%zu\n",
                st.n_pretokens, st.n_mapped_codepoints, st.n_merged_symbols,
                st.n_vocab_hits, st.n_vocab_misses);
    }

    free(line);
    gt_buf_free(&doc);
    gt_ids_free(&ids);
    gt_tokenizer_free(tk);
    return status;
}
