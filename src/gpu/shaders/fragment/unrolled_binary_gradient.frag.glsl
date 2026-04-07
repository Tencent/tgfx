// Copyright (C) 2026 Tencent. All rights reserved.
// unrolled_binary_gradient.frag.glsl - UnrolledBinaryGradientColorizer modular shader function.
// Binary search tree over scale/bias intervals to compute gradient color.
//
// Required macros:
//   TGFX_UBGC_INTERVAL_COUNT - Number of intervals (1-8)

#ifndef TGFX_UBGC_INTERVAL_COUNT
#define TGFX_UBGC_INTERVAL_COUNT 1
#endif

vec4 TGFX_UnrolledBinaryGradientColorizer(vec4 inputColor,
    vec4 scale0_1, vec4 bias0_1,
    vec4 thresholds1_7
#if TGFX_UBGC_INTERVAL_COUNT > 1
    , vec4 scale2_3, vec4 bias2_3
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 2
    , vec4 scale4_5, vec4 bias4_5
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 3
    , vec4 scale6_7, vec4 bias6_7
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 4
    , vec4 scale8_9, vec4 bias8_9, vec4 thresholds9_13
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 5
    , vec4 scale10_11, vec4 bias10_11
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 6
    , vec4 scale12_13, vec4 bias12_13
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 7
    , vec4 scale14_15, vec4 bias14_15
#endif
) {
    float t = inputColor.x;
    vec4 scale, bias;

#if TGFX_UBGC_INTERVAL_COUNT >= 4
    if (t < thresholds1_7.w) {
#endif

    // Binary tree level 1 (intervals 0-3)
#if TGFX_UBGC_INTERVAL_COUNT >= 2
    if (t < thresholds1_7.y) {
#endif
        if (t < thresholds1_7.x) {
            scale = scale0_1;
            bias = bias0_1;
#if TGFX_UBGC_INTERVAL_COUNT > 1
        } else {
            scale = scale2_3;
            bias = bias2_3;
#endif
        }
#if TGFX_UBGC_INTERVAL_COUNT > 2
    } else {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 3
        if (t < thresholds1_7.z) {
            scale = scale4_5;
            bias = bias4_5;
#if TGFX_UBGC_INTERVAL_COUNT > 3
        } else {
            scale = scale6_7;
            bias = bias6_7;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 2
    }
#endif

#if TGFX_UBGC_INTERVAL_COUNT > 4
    } else {
#endif

    // Binary tree level 2 (intervals 4-7)
#if TGFX_UBGC_INTERVAL_COUNT >= 6
    if (t < thresholds9_13.y) {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 5
        if (t < thresholds9_13.x) {
            scale = scale8_9;
            bias = bias8_9;
#if TGFX_UBGC_INTERVAL_COUNT > 5
        } else {
            scale = scale10_11;
            bias = bias10_11;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT > 6
    } else {
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 7
        if (t < thresholds9_13.z) {
            scale = scale12_13;
            bias = bias12_13;
#if TGFX_UBGC_INTERVAL_COUNT > 7
        } else {
            scale = scale14_15;
            bias = bias14_15;
#endif
        }
#endif
#if TGFX_UBGC_INTERVAL_COUNT >= 6
    }
#endif

#if TGFX_UBGC_INTERVAL_COUNT >= 4
    }
#endif

    return vec4(t * scale + bias);
}
