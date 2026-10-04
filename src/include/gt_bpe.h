/* gt_bpe.h — merge ranks and the merge loop.
 *
 * BPE here operates on *mapped codepoints*, not bytes. A pretoken arrives from
 * the byte-map stage as a sequence of codepoints in the GPT-2 alphabet, each
 * one initially its own symbol, and merges concatenate adjacent symbols. That
 * is the same thing the original GPT-2 does when it writes `tuple(token)`: its
 * "characters" are the mapped codepoints.
 *
 * Merge rules are keyed by the concatenation of the two symbols, so a rule
 * "A B" becomes one key A+B. Ranks are the rule's position in the merges list:
 * lower rank wins.
 */
#ifndef GT_BPE_H
#define GT_BPE_H

#include "gt_common.h"

/* A symbol is a run of mapped codepoints inside the pretoken's array. */
typedef struct {
    uint32_t off; /* index into the caller's codepoint array */
    uint32_t len; /* length in codepoints */
} gt_symbol;

typedef struct gt_bpe gt_bpe;

/* Build from GPT-2-format merge strings: each is "A B" where A and B are
 * codepoint sequences already in the mapped alphabet, given as UTF-8 text.
 * `count` rules in priority order; index in the list is the rank. */
gt_status gt_bpe_new(const char *const *rules, size_t count, gt_bpe **out);
void gt_bpe_free(gt_bpe *b);
size_t gt_bpe_rule_count(const gt_bpe *b);

/* Rank of the pair (left, right), or -1 if the pair is not a rule. The
 * concatenated key is formed without allocating. */
/* Hash-table rank query that bypasses the direct single-byte table, for
 * tests that prove the two paths agree. Not for production use. */
int32_t gt_bpe_rank_hashonly(const gt_bpe *b, const uint16_t *cps, gt_symbol left,
                             gt_symbol right);
int32_t gt_bpe_rank(const gt_bpe *b, const uint16_t *cps, gt_symbol left,
                    gt_symbol right);

/* Merge one pretoken in place-ish: `cps`/`ncps` is the mapped pretoken, and
 * `syms` receives the final symbols (at most ncps of them). Returns the number
 * of symbols written. Allocation-free apart from the caller's arrays, so a
 * mismatch can be attributed to the merge loop rather than to a buffer.
 *
 * The algorithm is the original's: repeatedly find the lowest-ranked adjacent
 * pair, then replace every leftmost non-overlapping occurrence of it, until no
 * adjacent pair is a rule.
 */
size_t gt_bpe_merge(const gt_bpe *b, const uint16_t *cps, size_t ncps,
                    gt_symbol *syms, size_t syms_cap);

#endif /* GT_BPE_H */