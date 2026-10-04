/* gt_pretok.c — GPT-2 pretokenization.
 *
 * Written as a direct transcription of the pattern's alternation, one branch
 * at a time, in the pattern's own order. The point is that a reader (or a
 * differential harness) can say which branch produced a pretoken, so this file
 * favours the shape of the pattern over speed or brevity.
 */
#include "gt_pretok.h"

#include <string.h>

#include "gt_byte_class.h"
#include "gt_bytemap.h"
#include "gt_unicode.h"

void gt_pretok_iter_init(gt_pretok_iter *it, gt_bytes input) {
    it->base = input.ptr;
    it->len = input.len;
    it->pos = 0;
}

/* Peek the class of the character starting at `p`, without consuming.
 * `*adv` receives how many bytes it occupies (>=1, always). */
/* The scanner. One symbol at a time, from the frozen contract:
 *
 *   00..7F  ordinary byte, existing ASCII handling
 *   80..BF  NEVER a lead. Consume exactly 1. The class is the Unicode
 *           category of that byte's GPT-2 byte-alphabet symbol -- 0x89 is
 *           U+012B (letter), 0xBD is U+00BD (number), 0xAB is U+00AB (other).
 *           These bytes only LOOK like continuations to a UTF-8 parser; in
 *           this alphabet they are ordinary characters. No following byte
 *           sequence may ever make this consume more than one byte.
 *   C0..F4  lead-shaped. Propose 2 (C0..DF), 3 (E0..EF) or 4 (F0..F4) bytes.
 *           If the following bytes really are continuations, assemble and
 *           classify the ASSEMBLED value -- even when it is not a valid
 *           scalar, since an overlong '/' or a surrogate is still assembled
 *           and classified here. If the structure fails, consume 1 as Other.
 *   F5..FF  not lead-shaped. Consume 1, byte-alphabet class.
 */
static gt_cp_class peek_class(const uint8_t *b, size_t len, size_t p,
                              size_t *adv) {
    uint8_t byte = b[p];
    *adv = 1;

    /* ASCII. Whitespace is a property of the BYTE: 0x20's byte-alphabet symbol
     * is U+0120, a letter, so classifying the mapped codepoint here would turn
     * every space into a letter and glue "a b" into one run. */
    if (byte < 0x80) {
        if (byte == 0x20 || (byte >= 0x09 && byte <= 0x0D)) return GT_CLS_SPACE;
        uint32_t cp = byte;
        if (gt_cp_is_letter(cp)) return GT_CLS_LETTER;
        if (gt_cp_is_number(cp)) return GT_CLS_NUMBER;
        return GT_CLS_OTHER;
    }

    /* Non-lead-shaped high bytes: never assemble. */
    if (byte < 0xC0 || byte >= 0xF5) {
        switch (GT_BYTE_CLASS[byte - 0x80]) {
        case 'L': return GT_CLS_LETTER;
        case 'N': return GT_CLS_NUMBER;
        case 'S': return GT_CLS_SPACE;
        default:  return GT_CLS_OTHER;
        }
    }

    /* Lead-shaped: only assemble when the structure is really there. */
    size_t w = (byte < 0xE0) ? 2 : ((byte < 0xF0) ? 3 : 4);
    if (p + w <= len) {
        size_t k = 1;
        for (; k < w; k++) {
            if ((b[p + k] & 0xC0) != 0x80) break;
        }
        if (k == w) {
            uint32_t cp;
            if (w == 2) cp = (uint32_t)(((b[p] & 0x1F) << 6) | (b[p + 1] & 0x3F));
            else if (w == 3)
                cp = (uint32_t)(((b[p] & 0x0F) << 12) | ((b[p + 1] & 0x3F) << 6) |
                                (b[p + 2] & 0x3F));
            else
                cp = (uint32_t)(((b[p] & 0x07) << 18) | ((b[p + 1] & 0x3F) << 12) |
                                ((b[p + 2] & 0x3F) << 6) | (b[p + 3] & 0x3F));
            if (cp > 0x10FFFF) cp = 0x10FFFF; /* clamp, as the reference does */
            if (gt_cp_is_letter(cp)) { *adv = w; return GT_CLS_LETTER; }
            if (gt_cp_is_number(cp)) { *adv = w; return GT_CLS_NUMBER; }
            if (gt_cp_is_space(cp))   { *adv = w; return GT_CLS_SPACE; }
            *adv = w; return GT_CLS_OTHER;
        }
    }
    /* Malformed or truncated lead: one byte, Other. */
    return GT_CLS_OTHER;
}

/* An ASCII literal at the cursor: "'s", "'t", "'re", "'ve", "'m", "'ll", "'d".
 * Returns the length matched, or 0. Case-sensitive, matching the pattern. */
static size_t match_contraction(const uint8_t *b, size_t len, size_t p) {
    if (p >= len || b[p] != '\'') return 0;
    if (p + 1 >= len) return 0;
    switch (b[p + 1]) {
        case 's':
        case 't':
        case 'm':
        case 'd':
            return 2;
        case 'r':
            if (p + 2 < len && b[p + 2] == 'e') return 3;
            return 0;
        case 'v':
            if (p + 2 < len && b[p + 2] == 'e') return 3;
            return 0;
        case 'l':
            if (p + 2 < len && b[p + 2] == 'l') return 3;
            return 0;
        default:
            return 0;
    }
}

int gt_pretok_next(gt_pretok_iter *it, gt_bytes *out) {
    const uint8_t *b = it->base;
    size_t len = it->len;
    size_t p = it->pos;

    if (p >= len) return 0;

    size_t start = p;

    /* Branch 1: 's 't 're 've 'm 'll 'd */
    size_t n = match_contraction(b, len, p);
    if (n) {
        it->pos = p + n;
        out->ptr = b + start;
        out->len = n;
        return 1;
    }

    /* Branches 2-4 all have the shape ` ?X+`: an optional single leading
     * space, then a run of one class. The space is ASCII 0x20 only — the
     * pattern writes a literal space, not \s. */
    if (b[p] == ' ' && p + 1 < len) {
        /* Only consume the space if something after it can start a run. A
         * trailing lone space belongs to the \s+ branches instead. */
        size_t adv;
        gt_cp_class c = peek_class(b, len, p + 1, &adv);
        /* Anything that is not whitespace can follow the optional space:
         * letter, number, or other -- and an invalid byte IS 'other'. */
        if (c != GT_CLS_SPACE) {
            p += 1;
        }
    }

    size_t adv;
    gt_cp_class c = peek_class(b, len, p, &adv);
    if (c == GT_CLS_LETTER || c == GT_CLS_NUMBER || c == GT_CLS_INVALID_UTF8) {
        /* ` ?\p{L}+` and ` ?\p{N}+` both mean "one or more", and an invalid
         * byte lands in the `other` class, so it joins branch 4 below. */
        gt_cp_class want =
            (c == GT_CLS_INVALID_UTF8) ? GT_CLS_INVALID_UTF8 : c;
        size_t q = p;
        size_t last = p;
        while (q < len) {
            size_t a;
            gt_cp_class cc = peek_class(b, len, q, &a);
            if (want == GT_CLS_INVALID_UTF8) {
                if (cc != GT_CLS_INVALID_UTF8) break;
            } else if (cc != want) {
                break;
            }
            q += a;
            last = q;
        }
        if (last > p) {
            it->pos = last;
            out->ptr = b + start;
            out->len = last - start;
            return 1;
        }
        /* Nothing matched after all; fall through to the whitespace and
         * catch-all branches with the cursor where it was. */
        p = start;
    }

    /* Branches 5 and 6: `\s+(?!\S)` then `\s+`.
     *
     * The lookahead is what gives an INTERIOR whitespace run its shape. A
     * greedy `\s+` that ends just before a non-space fails `(?![^\S])`, so the
     * engine gives characters back until the lookahead succeeds; the first
     * place it succeeds is one character before the non-space. That final
     * whitespace is therefore NOT part of this match -- it is left for the
     * leading space of the following ` ?X+` branch, which is why "a  b"
     * pretokenizes as "a", " ", " b" and not "a", "  ", "b".
     *
     * A run that reaches the end of input has no non-space after it, so
     * `\s+(?!\S)` matches all of it. A one-character run followed by a
     * non-space cannot be shortened at all, so branch 5 cannot match and
     * branch 6 takes the whole run. */
    {
        size_t a;
        if (peek_class(b, len, p, &a) == GT_CLS_SPACE) {
            size_t q = p, last = p;
            while (q < len) {
                size_t k;
                if (peek_class(b, len, q, &k) != GT_CLS_SPACE) break;
                q += k;
                last = q;
            }
            size_t take = last;
            if (q < len && last > p) {
                /* Start of the run's FINAL character -- i.e. the position
                 * we were at before the step that reached `last`. */
                size_t prev = p, pq = p;
                while (pq < last) {
                    size_t k, before = pq;
                    peek_class(b, len, pq, &k);
                    pq += k;
                    if (pq == last) prev = before;
                }
                take = (prev > p) ? prev : last;
            }
            if (take > p) {
                it->pos = take;
                out->ptr = b + start;
                out->len = take - start;
                return 1;
            }
        }
    }

    /* Branch 4 proper: ` ?[^\s\p{L}\p{N}]+` — one or more "other" characters,
     * each of which may be an invalid byte. This is the catch-all that keeps
     * the scanner moving on arbitrary input. */
    {
        /* Scan from `p`, not `start`: if the optional leading space was
         * consumed above, `p` is past it, and rescanning from `start` would
         * break on that very space and never reach the run -- which is what
         * made " '" two pretokens instead of one. The emitted piece still
         * starts at `start`, so the space stays attached. */
        size_t q = p;
        bool any = false;
        while (q < len) {
            size_t k;
            gt_cp_class cc = peek_class(b, len, q, &k);
            if (cc == GT_CLS_SPACE || cc == GT_CLS_LETTER || cc == GT_CLS_NUMBER) break;
            q += k;
            any = true;
        }
        if (any) {
            it->pos = q;
            out->ptr = b + start;
            out->len = q - start;
            return 1;
        }
    }

    /* Unreachable for well-formed class tables: every character is in one of
     * the four classes, so one of the branches above must have fired. If the
     * tables were ever generated wrong this is where it shows, as a
     * single-byte step rather than an infinite loop. */
    it->pos = start + 1;
    out->ptr = b + start;
    out->len = 1;
    return 1;
}