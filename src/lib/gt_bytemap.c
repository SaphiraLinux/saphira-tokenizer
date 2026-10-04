/* gt_bytemap.c — the byte-level alphabet.
 *
 * The mapping itself is a generated 256-entry table
 * (`tools/gen_bytemap_table.py` -> `include/gt_byte_table.h`), because this
 * function runs once per input byte and a 256-iteration count per call is not
 * a cost an oracle has to pay. The table is derived from the three-run rule,
 * and `test_bytemap` verifies every entry, so the rule and the table cannot
 * drift apart silently.
 */
#include "gt_bytemap.h"

#include <stdbool.h>

#include "gt_byte_table.h"

/* Does this byte map to itself? Exactly the three runs in the specification. */
static bool byte_is_itself(uint8_t b) {
    return (b >= 0x21 && b <= 0x7E) || (b >= 0xA1 && b <= 0xAC) ||
           b >= 0xAE; /* b is uint8_t, so 0xFF is the top */
}

uint32_t gt_byte_to_cp(uint8_t b) {
    /* One load. `byte_is_itself` below remains the documented rule (used by
     * `gt_cp_to_byte` and by the generator); this table is its memoisation. */
    return GT_BYTE_TO_CP[b];
}

int gt_cp_to_byte(uint32_t cp) {
    if (cp <= 0xFF) {
        if (byte_is_itself((uint8_t)cp)) return (int)cp;
        return -1;
    }
    if (cp < 0x100 || cp > GT_ALPHABET_MAX_CP) return -1;
    uint32_t want = cp - 0x100u;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 256; i++) {
        if (byte_is_itself((uint8_t)i)) continue;
        if (n == want) return (int)i;
        n++;
    }
    return -1;
}