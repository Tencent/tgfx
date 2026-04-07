// Copyright (C) 2026 Tencent. All rights reserved.
// const_color.glsl - ConstColorProcessor modular shader function.
// Compile switch: TGFX_CONST_COLOR_INPUT_MODE
//   0 = Ignore  (output = u_Color)
//   1 = ModulateRGBA (output = u_Color * inputColor)
//   2 = ModulateA (output = u_Color * inputColor.a)

tgfx_float4 FP_ConstColor(tgfx_float4 inputColor, tgfx_float4 u_Color) {
    tgfx_float4 result = u_Color;
#if TGFX_CONST_COLOR_INPUT_MODE == 1
    result *= inputColor;
#elif TGFX_CONST_COLOR_INPUT_MODE == 2
    result *= inputColor.a;
#endif
    return result;
}
