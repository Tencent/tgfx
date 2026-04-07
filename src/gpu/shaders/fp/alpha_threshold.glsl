// Copyright (C) 2026 Tencent. All rights reserved.
// alpha_threshold.glsl - AlphaThresholdFragmentProcessor modular shader function.
// Applies binary alpha thresholding: alpha becomes 0 or 1 based on threshold comparison.

tgfx_float4 FP_AlphaThreshold(tgfx_float4 inputColor, float u_Threshold) {
    tgfx_float4 result = tgfx_float4(0.0);
    if (inputColor.a > 0.0) {
        result.rgb = inputColor.rgb / inputColor.a;
        result.a = step(u_Threshold, inputColor.a);
        result = clamp(result, 0.0, 1.0);
    }
    return result;
}
