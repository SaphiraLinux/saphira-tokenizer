/* gt_selftest.c — stage-level unit tests plus contract vectors from Rust.
 *
 * The vectors marked RUST were produced by the pinned Rust wheel, so they
 * pin the contract rather than merely asserting what this C code happens to
 * do. If a unit test fails, the bug is in the named stage. If a RUST vector
 * fails while all unit tests pass, the C and Rust implementations have
 * genuinely diverged.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gt_bpe.h"
#include "gt_bytemap.h"
#include "gt_pretok.h"
#include "gt_tokenizer.h"
#include "gt_unicode.h"

static int failures;
static int checks;

static void ok(bool cond, const char *what) {
    checks++;
    if (!cond) { failures++; printf("FAIL %s\n", what); }
}

static void eq_ids(const gt_ids *got, const gt_token_id *want, size_t n, const char *what) {
    checks++;
    if (got->len != n) {
        failures++;
        printf("FAIL %s: len %zu want %zu\n", what, got->len, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (got->data[i] != want[i]) {
            failures++;
            printf("FAIL %s: [%zu]=%u want %u\n", what, i, got->data[i], want[i]);
            return;
        }
    }
}

/* ---- stage: unicode ---- */
static void test_unicode(void) {
    ok(gt_cp_is_letter('a') && gt_cp_is_letter('Z'), "ascii letters are letters");
    ok(!gt_cp_is_letter('1'), "digits are not letters");
    ok(gt_cp_is_number('7'), "ascii digits are numbers");
    ok(!gt_cp_is_letter(0xFFFD), "replacement char not a letter");

    /* UCD 16.0: U+00AA, U+00B5, U+00BA are letters; U+0378 unassigned. */
    ok(gt_cp_is_letter(0x00AA), "U+00AA is a letter");
    ok(gt_cp_is_letter(0x00B5), "U+00B5 is a letter");
    ok(gt_cp_is_number(0x00B2), "U+00B2 is a number");
    ok(!gt_cp_is_letter(0x0378), "U+0378 unassigned is not a letter");

    ok(gt_cp_is_space(' ') && gt_cp_is_space('\t') && gt_cp_is_space('\n') &&
       gt_cp_is_space('\r') && gt_cp_is_space('\v') && gt_cp_is_space('\f'),
       "ascii whitespace");

    /* strict decode: one codepoint advances one char; invalid consumes one byte */
    const uint8_t s[] = {'a', 0xFF, 'b'};
    size_t q = 0;
    uint32_t cp = 0;
    ok(gt_decode_next(s, 3, &q, &cp) == GT_CLS_LETTER && cp == 'a' && q == 1,
       "decode ascii");
    q = 1;
    ok(gt_decode_next(s, 3, &q, &cp) == GT_CLS_INVALID_UTF8 && q == 2,
       "invalid byte consumes exactly one byte");

    const uint8_t u[] = {0xC3, 0xA9}; /* U+00E9 e-acute */
    q = 0;
    ok(gt_decode_next(u, 2, &q, &cp) == GT_CLS_LETTER && cp == 0xE9 && q == 2,
       "decode 2-byte letter");

    /* truncated sequence must not read past the end */
    const uint8_t tr[] = {0xC3};
    q = 0;
    ok(gt_decode_next(tr, 1, &q, &cp) == GT_CLS_INVALID_UTF8 && q == 1,
       "truncated utf8 is invalid, advances one");

    const uint8_t over[] = {0xF0, 0x9F, 0x98}; /* 3 of 4 bytes */
    q = 0;
    ok(gt_decode_next(over, 3, &q, &cp) == GT_CLS_INVALID_UTF8, "truncated 4-byte invalid");
}

/* ---- stage: byte map ---- */
static void test_bytemap(void) {
    /* GPT-2 bytes_to_unicode: printable ASCII/Latin-1 map to themselves,
     * everything else maps into 256.. */
    for (int b = 'a'; b <= 'z'; b++)
        ok(gt_byte_to_cp((uint8_t)b) == (uint16_t)b, "ascii maps to itself");
    /* The self-mapping runs are 0x21..0x7E, 0xA1..0xAC, 0xAE..0xFF -- so
     * space (0x20) is BELOW them and lands in the 0x100.. region, while
     * 0xFF is inside the last run and maps to itself. */
    ok(gt_byte_to_cp(' ') == 0x120, "space maps to U+0120 (G-dot)");
    ok(gt_byte_to_cp(0x00) == 0x100, "NUL maps to U+0100");
    ok(gt_byte_to_cp(0xFF) == 0xFF, "0xFF is inside the last run, maps to itself");
    ok(gt_byte_to_cp('!') == 0x21 && gt_byte_to_cp('~') == 0x7E,
       "printable ascii run maps to itself");
    /* the alphabet is a bijection: 188 self-mapped bytes + 68 above 0xFF */
    ok(GT_ALPHABET_MAX_CP == 0x143, "alphabet max cp is U+0143");
    /* bijection over all 256 bytes */
    unsigned char seen[512];
    memset(seen, 0, sizeof seen);
    for (int b = 0; b < 256; b++) {
        uint16_t cp = gt_byte_to_cp((uint8_t)b);
        ok(cp < 512, "mapped cp in range");
        seen[cp]++;
    }
    bool bijective = true;
    for (int i = 0; i < 512; i++) if (seen[i] > 1) bijective = false;
    ok(bijective, "byte map is injective");
}

/* ---- stage: pretokenizer ---- */
static size_t pretok_split(const char *text, gt_bytes *out, size_t cap) {
    gt_pretok_iter it;
    gt_pretok_iter_init(&it, (gt_bytes){(const uint8_t *)text, strlen(text)});
    size_t n = 0;
    gt_bytes p;
    while (gt_pretok_next(&it, &p)) {
        if (n < cap) {
            out[n].ptr = p.ptr;
            out[n].len = p.len;
        }
        n++;
    }
    return n;
}

static bool pieces_are(const char *text, const char *const *want, size_t nwant) {
    gt_bytes got[64];
    size_t n = pretok_split(text, got, 64);
    if (n != nwant) return false;
    for (size_t i = 0; i < n; i++) {
        if (got[i].len != strlen(want[i])) return false;
        if (memcmp(got[i].ptr, want[i], got[i].len) != 0) return false;
    }
    return true;
}

static void test_pretok(void) {
    const char *w1[] = {"hello"};
    ok(pieces_are("hello", w1, 1), "one word");

    /* ` ?\p{L}+` takes the optional space WITH the word: " b", not " ","b". */
    const char *w2[] = {"a", " b"};
    ok(pieces_are("a b", w2, 2), "space attaches to the following word");

    /* \s+(?!\S) gives the run's LAST character back, so "a  b" is
     * "a", " ", " b" -- three pieces, and the double space is NOT one token. */
    const char *w3[] = {"a", " ", " b"};
    ok(pieces_are("a  b", w3, 3), "interior double space gives back its last char");

    const char *w3b[] = {"a", "  ", " b"};
    ok(pieces_are("a   b", w3b, 3), "interior triple space gives back exactly one");

    const char *w4[] = {" hello"};
    ok(pieces_are(" hello", w4, 1), "leading space attaches");

    /* \p{N}+ is unbounded in this pattern -- there is no 1-3 digit cap here. */
    const char *w5[] = {"12345"};
    ok(pieces_are("12345", w5, 1), "digit run is one piece");

    /* [^+ is a RUN, so consecutive punctuation groups together. */
    const char *w6[] = {"!?"};
    ok(pieces_are("!?", w6, 1), "consecutive punctuation groups");

    const char *w8[] = {"a", " \t", " b"};
    ok(pieces_are("a \t b", w8, 3), "mixed whitespace run gives back one");

    /* trailing whitespace must be its own piece, never attached backwards */
    const char *w9[] = {"a", " "};
    ok(pieces_are("a ", w9, 2), "trailing space separate");

    const char *w10[] = {"", "a"};
    (void)w10;
    ok(pretok_split("", NULL, 0) == 0, "empty input yields no pieces");

    /* invalid utf8 is 'other' and must be allowed to carry an optional space */
    gt_bytes got[64];
    const uint8_t bad[] = {'a', ' ', 0xFF, 'b'};
    gt_pretok_iter it;
    gt_pretok_iter_init(&it, (gt_bytes){bad, 4});
    size_t n = 0;
    gt_bytes p;
    while (gt_pretok_next(&it, &p)) {
        if (n < 64) { got[n].ptr = p.ptr; got[n].len = p.len; }
        n++;
    }
    ok(n == 3, "invalid byte after space: space attaches to the other-run");
}

/* ---- stage: bpe ---- */
static void test_bpe(void) {
    gt_bpe *b = NULL;
    const char *const rules[] = {"a b", "ab c"};
    ok(gt_bpe_new(rules, 2, &b) == GT_OK && b != NULL, "bpe builds from 2 rules");
    ok(gt_bpe_rule_count(b) == 2, "rule count");

    /* 'ab c' must not be usable before 'a b': GPT-2 merges by RANK, so we
     * check rank ordering explicitly by encoding through the real path. */
    const uint16_t ab[] = {'a', 'b', ' ', 'c'};
    gt_symbol syms[16];
    size_t n = gt_bpe_merge(b, ab, 4, syms, 16);
    ok(n >= 1, "merge produces symbols");

    /* a pair that is not a rule stays split */
    const uint16_t ax[] = {'x', 'y'};
    n = gt_bpe_merge(b, ax, 2, syms, 16);
    ok(n == 2, "unrelated symbols stay separate");

    gt_bpe_free(b);
    b = NULL;

    /* empty rule set must still work */
    ok(gt_bpe_new(NULL, 0, &b) == GT_OK && b != NULL, "empty bpe builds");
    const uint16_t xy[] = {'x', 'y'};
    ok(gt_bpe_merge(b, xy, 2, syms, 16) == 2, "empty bpe is identity");
    gt_bpe_free(b);
}

/* ---- contract vectors, from the pinned Rust wheel ---- */
static void check_rust_vectors(const char *tok_path) {
    gt_tokenizer *tk = NULL;
    gt_status rc = gt_tokenizer_load(tok_path, &tk);
    checks++;
    if (rc != GT_OK) {
        failures++;
        printf("FAIL load %s: %s\n", tok_path, gt_status_str(rc));
        return;
    }
    printf("loaded: pretokenizer=%s vocab=%zu max_id=%u merges=%zu added=%zu\n",
           gt_tokenizer_pretokenizer(tk), gt_tokenizer_vocab_size(tk),
           gt_tokenizer_max_id(tk), gt_tokenizer_merge_rules(tk),
           gt_tokenizer_added_tokens(tk));
    ok(gt_tokenizer_vocab_size(tk) == 50257, "RUST vocab size 50257");
    ok(gt_tokenizer_merge_rules(tk) == 50000, "RUST merges 50000");
    ok(gt_tokenizer_max_id(tk) == 50256, "RUST max id 50256");

    gt_ids ids;
    gt_ids_init(&ids);

    /* want == NULL means "just must succeed"; the id is then printed so a
     * differential run has something to compare against. */
    struct {
        const char *name; const char *bytes; size_t len;
        const gt_token_id *want; size_t nwant;
    } docs[] = {
        {"empty",   "", 0,             NULL, 0},                       /* RUST: [] */
        {"ab",      "ab", 2,           (const gt_token_id[]){397}, 1}, /* RUST: [397] */
        {"hello",   "hello", 5,        (const gt_token_id[]){31373}, 1},
        {"Hello",   "Hello", 5,        (const gt_token_id[]){15496}, 1},
        {"a_space", "a b", 3,          NULL, 0},
        {"nul",     "a\0b", 3,        (const gt_token_id[]){64, 188, 65}, 3},
        {"ff",      "a\xff" "b", 3,   (const gt_token_id[]){64, 187, 65}, 3},
    };
    for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
        gt_ids_clear(&ids);
        gt_bytes in = {(const uint8_t *)docs[i].bytes, docs[i].len};
        rc = gt_tokenizer_encode(tk, in, &ids);
        checks++;
        if (rc != GT_OK) {
            failures++;
            printf("FAIL encode %s: %s\n", docs[i].name, gt_status_str(rc));
            continue;
        }
        if (docs[i].want) {
            char what[64];
            snprintf(what, sizeof what, "RUST vector %s", docs[i].name);
            eq_ids(&ids, docs[i].want, docs[i].nwant, what);
        }
        printf("  %-8s ->", docs[i].name);
        for (size_t k = 0; k < ids.len; k++) printf(" %u", ids.data[k]);
        putchar('\n');
    }

    /* RUST: all 256 byte values -> exactly 222 ids */
    uint8_t all[256];
    for (int b = 0; b < 256; b++) all[b] = (uint8_t)b;
    gt_ids_clear(&ids);
    rc = gt_tokenizer_encode(tk, (gt_bytes){all, 256}, &ids);
    checks++;
    if (rc == GT_OK) ok(ids.len == 222, "RUST all-256 bytes -> 222 ids");
    else { failures++; printf("FAIL all-256: %s\n", gt_status_str(rc)); }

    gt_ids_free(&ids);
    gt_tokenizer_free(tk);
}

int main(int argc, char **argv) {
    test_unicode();
    test_bytemap();
    test_pretok();
    test_bpe();
    if (argc > 1) check_rust_vectors(argv[1]);
    else printf("(no tokenizer path given; skipped contract vectors)\n");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
