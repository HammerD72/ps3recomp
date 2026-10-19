/* Checked host byte-shuffle implementation. Header-only so standalone lifted
 * callers retain their existing link contract. No process-wide ISA change. */
#ifndef PS3RECOMP_SPU_SHUFFLE_H
#define PS3RECOMP_SPU_SHUFFLE_H
#include "../../include/ps3emu/ps3types.h"
#include <string.h>

#if !defined(SPU_SHUFB_FORCE_SCALAR) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#define SPU_SHUFB_X86 1
#include <tmmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#else
#define SPU_SHUFB_X86 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline u128 spu_shufb_ref(u128 a, u128 b, u128 c) {
    u128 out;
    for (unsigned h = 0; h < 16; ++h) {
        unsigned selector = c._u8[h];
        unsigned index = (selector & 15) ^ 3; /* SPU byte to host word-value layout */
        if (selector & 128)
            out._u8[h] = (selector & 64) ? ((selector & 32) ? 128 : 255) : 0;
        else
            out._u8[h] = (selector & 16) ? b._u8[index] : a._u8[index];
    }
    return out;
}

static inline int spu_shufb_simd_available(void) {
#if SPU_SHUFB_X86
#ifdef _MSC_VER
    /* Per-thread cache avoids a data race between standalone C callers. SSSE3
     * uses only the SSE register state already supported by the host ABI. */
    static __declspec(thread) int cached;
    if (!cached) {
        int registers[4];
        __cpuid(registers, 1);
        cached = (registers[2] & (1 << 9)) ? 2 : 1;
    }
    return cached == 2;
#else
    return __builtin_cpu_supports("ssse3") != 0;
#endif
#else
    return 0;
#endif
}

#if SPU_SHUFB_X86
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("ssse3")))
#endif
static inline u128 spu_shufb_ssse3(u128 a, u128 b, u128 c) {
    __m128i av, bv, cv;
    memcpy(&av, &a, 16); memcpy(&bv, &b, 16); memcpy(&cv, &c, 16);
    /* Output and control bytes already share host positions. Only the source
     * index needs the within-word reversal; no three extra vector reversals. */
    __m128i index = _mm_and_si128(_mm_xor_si128(cv, _mm_set1_epi8(3)), _mm_set1_epi8(15));
    __m128i from_b = _mm_cmpeq_epi8(_mm_and_si128(cv, _mm_set1_epi8(16)), _mm_set1_epi8(16));
    __m128i value = _mm_or_si128(_mm_andnot_si128(from_b, _mm_shuffle_epi8(av, index)),
                               _mm_and_si128(from_b, _mm_shuffle_epi8(bv, index)));
    __m128i high = _mm_cmpgt_epi8(_mm_setzero_si128(), cv);
    __m128i c0 = _mm_cmpeq_epi8(_mm_and_si128(cv, _mm_set1_epi8((char)0xc0)), _mm_set1_epi8((char)0xc0));
    __m128i e0 = _mm_cmpeq_epi8(_mm_and_si128(cv, _mm_set1_epi8((char)0xe0)), _mm_set1_epi8((char)0xe0));
    __m128i special = _mm_or_si128(_mm_andnot_si128(e0, c0), _mm_and_si128(e0, _mm_set1_epi8((char)0x80)));
    value = _mm_or_si128(_mm_andnot_si128(high, value), special);
    u128 out;
    memcpy(&out, &value, 16);
    return out;
}
#endif

static inline u128 spu_shufb(u128 a, u128 b, u128 c) {
#if SPU_SHUFB_X86
    if (spu_shufb_simd_available()) return spu_shufb_ssse3(a, b, c);
#endif
    return spu_shufb_ref(a, b, c);
}

#ifdef __cplusplus
}
#endif
#endif
