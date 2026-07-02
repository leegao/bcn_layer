#pragma once

// Shared between slang and C++
#ifdef __cplusplus
#include <cstdint>
    #define STRUCT_ALIGN(x) alignas(x)
#else
    // Slang
    #define STRUCT_ALIGN(x)
#endif

struct STRUCT_ALIGN(16) AstcParameters {
    uint8_t ep0[4];
    uint8_t ep1[4];
    uint8_t weights[16];
};

struct STRUCT_ALIGN(16) AstcDebug {
    AstcParameters params;
};

struct STRUCT_ALIGN(16) AstcAnalysis {
    uint32_t count;
    uint64_t sum_color_spread_squared;
    uint64_t sum_weights;
    uint64_t sum_weights_squared;
};
