/* gt_simd.h — AVX2 kernels with scalar fallback.
 *
 * Used only where the operation is dense and data-parallel: comparing
 * codepoint keys (vocab/BPE verification) and classifying ASCII bytes.
 * Pointer-chasing (hash probes, binary search steps) stays scalar because
 * SIMD cannot fix a dependent load chain. Every kernel has a scalar path
 * that runs when AVX2 is absent, so the binary is portable and the oracle
 * stays exact on any x86-64.
 */
#ifndef GT_SIMD_H
#define GT_SIMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

/* AVX2 available at runtime (always true when compiled with -mavx2). */
static inline int gt_has_avx2(void) {
#ifdef __AVX2__
    return 1;
#else
    return __builtin_cpu_supports("avx2");
#endif
}

/* Compare n u16 codepoints. Returns 0 iff equal. AVX2 compares 16 lanes at
 * once; the tail and the no-AVX2 path are scalar. Semantics identical to
 * memcmp on the same bytes (little-endian u16s compare equal iff all equal;
 * ordering is decided by the caller, which only needs equality here). */
static inline int gt_u16_eq(const uint16_t *a, const uint16_t *b, size_t n) {
#ifdef __AVX2__
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        __m256i eq = _mm256_cmpeq_epi16(va, vb);
        if (_mm256_movemask_epi8(eq) != (int)0xFFFFFFFFu) return 1;
    }
    for (; i < n; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
#else
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
#endif
}

#endif /* GT_SIMD_H */
