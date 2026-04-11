// Copyright (C) 2026 Tencent. All rights reserved.
// gaussian_blur_1d.frag.glsl - GaussianBlur1DFragmentProcessor modular shader function.
// Performs 1D Gaussian blur by sampling the child FP at multiple offsets.
//
// Required macros:
//   TGFX_BLUR_LOOP_LIMIT    - maximum loop iterations (4 * maxSigma)
//   TGFX_GB1D_SAMPLE(coord) - expands to child FP texture sampling call at given coord

#ifndef TGFX_BLUR_LOOP_LIMIT
#define TGFX_BLUR_LOOP_LIMIT 24
#endif

vec4 TGFX_GaussianBlur1D(float sigma, vec2 step, vec2 baseCoord) {
    vec2 offset = step;
    int radius = int(ceil(2.0 * sigma));
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int j = 0; j <= TGFX_BLUR_LOOP_LIMIT; ++j) {
        int i = j - radius;
        float weight = exp(-float(i * i) / (2.0 * sigma * sigma));
        total += weight;
        vec2 sampleCoord = baseCoord + offset * float(i);
        sum += TGFX_GB1D_SAMPLE(sampleCoord) * weight;
        if (i == radius) { break; }
    }
    return sum / total;
}
