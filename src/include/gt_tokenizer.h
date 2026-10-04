/* gt_tokenizer.h — load a GPT-2 style tokenizer.json and encode bytes to ids.
 *
 * The stage sequence, each a separate translation unit so a disagreement with
 * another implementation localises:
 *
 *   input      gt_bytes, arbitrary bytes, no UTF-8 validation
 *   special    added/special token policy, applied before splitting
 *   pretok     GPT-2 pattern
 *   bytemap    byte -> mapped codepoint
 *   bpe        merge loop
 *   vocab      mapped codepoints -> id
 *   output     ids
 */
#ifndef GT_TOKENIZER_H
#define GT_TOKENIZER_H

#include "gt_bpe.h"
#include "gt_common.h"
#include "gt_special.h"
#include "gt_vocab.h"

typedef struct gt_tokenizer gt_tokenizer;

/* Load from a HuggingFace tokenizer.json with a ByteLevel pre_tokenizer and a
 * BPE model. Rejects anything else loudly rather than guessing: a normalizer
 * or a different pretokenizer would change the output, and silently ignoring
 * it would make this a wrong oracle rather than an obviously broken one. */
gt_status gt_tokenizer_load(const char *path, gt_tokenizer **out);
void gt_tokenizer_free(gt_tokenizer *t);

size_t gt_tokenizer_vocab_size(const gt_tokenizer *t);
gt_token_id gt_tokenizer_max_id(const gt_tokenizer *t);
size_t gt_tokenizer_merge_rules(const gt_tokenizer *t);
size_t gt_tokenizer_added_tokens(const gt_tokenizer *t);
const char *gt_tokenizer_pretokenizer(const gt_tokenizer *t);

/* Encode one document. `out` is appended to, so a caller can build a whole
 * corpus in one buffer. Arbitrary bytes are legal input. */
gt_status gt_tokenizer_encode(gt_tokenizer *t, gt_bytes input, gt_ids *out);

/* Diagnostic counters, filled by the last encode. Used by the differential
 * reporter to say which stage a mismatch belongs to. */
typedef struct {
    size_t n_pretokens;
    size_t n_mapped_codepoints;
    size_t n_merged_symbols;
    size_t n_vocab_hits;
    size_t n_vocab_misses;
} gt_encode_stats;

/* Thread-safe encode sharing read-only tables; stats go to the caller.
 * Concurrent calls on one tokenizer are safe; sharing `out`/`st` is not. */
gt_status gt_tokenizer_encode_mt(const gt_tokenizer *t, gt_bytes input, gt_ids *out,
                                 gt_encode_stats *st);

void gt_tokenizer_encode_stats(const gt_tokenizer *t, gt_encode_stats *out);

/* Stage accessors for the debugger and selftest agreement checks, so they
 * exercise the tokenizer's own tables instead of rebuilding them. */
const gt_bpe *gt_debug_bpe(const gt_tokenizer *t);
const gt_vocab *gt_debug_vocab(const gt_tokenizer *t);

#endif /* GT_TOKENIZER_H */
