// Copyright (C) 2026 Tencent. All rights reserved.
// alpha_threshold.glsl - AlphaThresholdFragmentProcessor modular shader function.
// Applies binary alpha thresholding: alpha becomes 0 or 1 based on threshold comparison.

vec4 TGFX_AlphaThreshold(vec4 inputColor, float threshold) {
    vec4 result = vec4(0.0);
    if (inputColor.a > 0.0) {
        result.rgb = inputColor.rgb / inputColor.a;
        result.a = step(threshold, inputColor.a);
        result = clamp(result, 0.0, 1.0);
    }
    return result;
}
