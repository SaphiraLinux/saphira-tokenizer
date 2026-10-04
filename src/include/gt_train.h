/* gt_train.h — BPE training (learn merges from a corpus), in C.
 *
 * No Python, no bindings: raw bytes in, tokenizer.json out. The learned
 * vocabulary and merges are written in the same GPT-2-style layout that
 * gt_tokenizer_load reads, so a trained model round-trips through this
 * implementation with no other tool involved.
 *
 * Method is the standard byte-level BPE: split the corpus into pretokens,
 * start every pretoken as its byte-alphabet symbols, then repeatedly merge
 * the most frequent adjacent pair until the vocabulary is full. Pair counts
 * are maintained incrementally (a merge only touches words containing that
 * pair) and the best pair comes from a max-heap with lazy deletion, so
 * training is O(merges * affected_words), not O(merges * corpus).
 *
 * Tie-breaking is deterministic (lowest pair by (first, second) symbol id,
 * then first-seen) and DOCUMENTED here because it is the one place a trained
 * model can legitimately differ from another implementation's: same counts,
 * different tie order, different merge, different ids downstream. That is a
 * training-policy difference, not an encoding bug, and the selftest pins it.
 */
#ifndef GT_TRAIN_H
#define GT_TRAIN_H

#include "gt_common.h"

/* One learned token: its byte string and id. Ids 0..255 are the byte alphabet
 * in byte order; learned merges follow in merge order. */
typedef struct {
    gt_buf bytes;
    gt_token_id id;
} gt_learned_token;

/* One learned merge: left + right -> new token id. */
typedef struct {
    gt_buf left;
    gt_buf right;
    gt_token_id id;
} gt_learned_merge;

typedef struct {
    gt_learned_token *toks;
    size_t ntoks;
    gt_learned_merge *merges;
    size_t nmerges;
} gt_trained;

void gt_trained_free(gt_trained *t);

/* Train on `ndocs` documents. Stops at `vocab_size` total tokens (including
 * the 256 byte symbols) or when no pair occurs twice. `specials` are reserved
 * ids appended after the learned vocabulary; they never participate in
 * merges. Returns GT_OK and fills `out`. */
gt_status gt_train(const gt_bytes *docs, size_t ndocs, size_t vocab_size,
                   const gt_bytes *specials, size_t nspecial, gt_trained *out);

/* Write a GPT-2-style tokenizer.json that gt_tokenizer_load accepts. */
gt_status gt_train_write_json(const gt_trained *t, const char *path);

#endif /* GT_TRAIN_H */
