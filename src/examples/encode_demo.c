/* encode_demo.c — the five-minute tour of saphira-tokenizer.
 *
 * Build from src/:  make examples   (or see examples/README.md)
 * Run:
 *   ./build/encode_demo <tokenizer.json> "hello world" "a  b" "café"
 *
 * Prints one line of space-separated token ids per input, followed by its
 * pretoken spans so you can see WHERE the boundaries fell.
 */
#include <stdio.h>
#include <string.h>

#include "gt_pretok.h"
#include "gt_tokenizer.h"

static void show_spans(const char *text) {
    gt_pretok_iter it;
    gt_pretok_iter_init(&it, (gt_bytes){(const uint8_t *)text, strlen(text)});
    gt_bytes p;
    printf("  spans:");
    while (gt_pretok_next(&it, &p)) {
        printf(" [");
        for (size_t i = 0; i < p.len; i++) {
            unsigned char c = p.ptr[i];
            if (c >= 0x20 && c < 0x7F) fputc(c, stdout);
            else printf("\\x%02x", c);
        }
        printf("]");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: encode_demo <tokenizer.json> <text> [text...]\n");
        return 2;
    }
    gt_tokenizer *tk = NULL;
    gt_status rc = gt_tokenizer_load(argv[1], &tk);
    if (rc != GT_OK) {
        fprintf(stderr, "load failed: %s\n", gt_status_str(rc));
        return 1;
    }
    printf("# vocab=%zu merges=%zu pretokenizer=%s\n",
           gt_tokenizer_vocab_size(tk), gt_tokenizer_merge_rules(tk),
           gt_tokenizer_pretokenizer(tk));

    gt_ids ids;
    gt_ids_init(&ids);
    for (int i = 2; i < argc; i++) {
        gt_ids_clear(&ids);
        gt_bytes in = {(const uint8_t *)argv[i], strlen(argv[i])};
        rc = gt_tokenizer_encode(tk, in, &ids);
        if (rc != GT_OK) {
            fprintf(stderr, "encode failed for \"%s\": %s\n", argv[i], gt_status_str(rc));
            continue;
        }
        printf("%s ->", argv[i]);
        for (size_t k = 0; k < ids.len; k++) printf(" %u", ids.data[k]);
        printf("\n");
        show_spans(argv[i]);
    }
    gt_ids_free(&ids);
    gt_tokenizer_free(tk);
    return 0;
}
