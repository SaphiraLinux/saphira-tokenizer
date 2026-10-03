/* gt_bytemap.c — the byte-level alphabet, built by the rule rather than a
 * literal table, so the rule is checkable against the specification.
 */
#include "gt_bytemap.h"

#include <stdbool.h>

/* Does this byte map to itself? Exactly the three runs in the specification. */
static bool byte_is_itself(uint8_t b) {
    return (b >= 0x21 && b <= 0x7E) || (b >= 0xA1 && b <= 0xAC) ||
           b >= 0xAE; /* b is uint8_t, so 0xFF is the top */
}

uint32_t gt_byte_to_cp(uint8_t b) {
    if (byte_is_itself(b)) return b;
    /* Count the non-self bytes below `b`; that count is the offset into the
     * 0x100.. region. Computing it rather than tabulating it keeps the
     * specification and the code in one place. */
    uint32_t n = 0;
    for (uint32_t i = 0; i < b; i++) {
        if (!byte_is_itself((uint8_t)i)) n++;
    }
    return 0x100u + n;
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