/* gt_json.h — a small read-only JSON scanner, just enough for tokenizer.json.
 *
 * Not a general JSON library and not trying to be one. It supports exactly
 * what a tokenizer file contains: objects, arrays, strings with \u escapes,
 * numbers, true/false/null. Anything else is a parse error rather than a
 * guess, because a silently mis-parsed vocabulary would produce wrong token
 * ids that look like a tokenizer bug.
 */
#ifndef GT_JSON_H
#define GT_JSON_H

#include "gt_common.h"

typedef struct {
    const char *s;
    size_t n;
    size_t i;
    int depth;
} gt_json;

void gt_json_init(gt_json *j, const char *text, size_t len);

void gt_ws(gt_json *j);
int gt_peek(gt_json *j);
bool gt_eat(gt_json *j, char c);
bool gt_lit(gt_json *j, const char *lit); /* matches a bare literal */

gt_status gt_json_string(gt_json *j, gt_buf *out); /* string -> decoded bytes */
gt_status gt_json_number_i64(gt_json *j, int64_t *out);
gt_status gt_json_skip_value(gt_json *j);
gt_status gt_json_object_key(gt_json *j, gt_buf *out); /* expects "key" : */

/* Iterate an object's members. Call after consuming '{'; returns GT_OK with
 * *done=1 at '}'. On each iteration *key is the member name and the cursor
 * sits just after the ':'. */
gt_status gt_json_obj_begin(gt_json *j, bool *done);
gt_status gt_json_obj_next(gt_json *j, bool *done);

/* Same for arrays; *done=1 at ']'. */
gt_status gt_json_arr_begin(gt_json *j, bool *done);
gt_status gt_json_arr_next(gt_json *j, bool *done);

#endif /* GT_JSON_H */
