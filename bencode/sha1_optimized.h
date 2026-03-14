#ifndef SHA1_OPTIMIZED_H
#define SHA1_OPTIMIZED_H

#include <stdint.h>
#include <stddef.h>

// Try to use CPU SHA extensions if available
#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h>
    #define HAS_SHA_EXTENSIONS 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
    #define HAS_SHA_EXTENSIONS 1
#else
    #define HAS_SHA_EXTENSIONS 0
#endif

// Fast SHA1 implementation
int sha1_fast(uint8_t *hash, const uint8_t *data, size_t len);

#endif
