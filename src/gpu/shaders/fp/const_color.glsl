// Copyright (C) 2026 Tencent. All rights reserved.
// const_color.glsl - ConstColorProcessor modular shader function.
// Compile switch: TGFX_CC_MODE
//   0 = Ignore  (output = color)
//   1 = ModulateRGBA (output = color * inputColor)
//   2 = ModulateA (output = color * inputColor.a)

vec4 TGFX_ConstColor(vec4 inputColor, vec4 color) {
    vec4 result = color;
#if TGFX_CC_MODE == 1
    result *= inputColor;
#elif TGFX_CC_MODE == 2
    result *= inputColor.a;
#endif
    return result;
}
