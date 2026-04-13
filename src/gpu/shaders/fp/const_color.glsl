// Copyright (C) 2026 Tencent. All rights reserved.
// const_color.glsl - ConstColorProcessor modular shader function.
// mode: 0 = Ignore (output = color), 1 = ModulateRGBA, 2 = ModulateA

vec4 TGFX_ConstColor(vec4 inputColor, vec4 color, int mode) {
    vec4 result = color;
    if (mode == 1) {
        result *= inputColor;
    } else if (mode == 2) {
        result *= inputColor.a;
    }
    return result;
}
