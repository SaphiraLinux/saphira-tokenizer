/* gt_vocab.h — vocabulary: mapped-codepoint sequence -> token id.
 *
 * Keys are the mapped-codepoint alphabet, so a lookup key is the same shape as
 * a merge key. The table is sorted and searched by binary search: the obvious
 * structure, and one that cannot introduce a hash-collision class of bug into
 * an oracle whose whole job is to be right.
 */
#ifndef GT_VOCAB_H
#define GT_VOCAB_H

#include "gt_common.h"

typedef struct gt_vocab gt_vocab;

/* Build from parallel arrays: `keys[i]` is `key_lens[i]` mapped codepoints and
 * maps to `ids[i]`. Copied, so the caller may free its own storage. */
gt_status gt_vocab_new(const uint16_t *keys, const size_t *key_lens,
                       const gt_token_id *ids, size_t count, gt_vocab **out);
void gt_vocab_free(gt_vocab *v);
size_t gt_vocab_size(const gt_vocab *v);
gt_token_id gt_vocab_max_id(const gt_vocab *v);

/* Id for a mapped-codepoint sequence, or GT_ID_NONE when absent.
 *
 * Absence is a real outcome, not an error: BPE can produce a sequence the
 * vocabulary does not contain only if the merge table is inconsistent, so the
 * caller reports it as a vocabulary-stage failure rather than silently
 * skipping. */
#define GT_ID_NONE ((gt_token_id)0xFFFFFFFFu)
gt_token_id gt_vocab_lookup(const gt_vocab *v, const uint16_t *cps, size_t n);
/* Binary-search reference for tests proving the hash agrees. */
gt_token_id gt_vocab_lookup_binary_ref(const gt_vocab *v, const uint16_t *cps, size_t n);

#endif /* GT_VOCAB_H */