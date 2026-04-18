// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_fs_boilerplate.glsl - Shared fragment shader boilerplate helpers.

/**
 * Performs perspective division on a homogeneous 3D coordinate, returning 2D.
 * Used when coord transforms carry a perspective row; the VS emits vec3 coords
 * and this helper divides xy by z in the FS.
 */
vec2 TGFX_PerspDivide(vec3 c) {
    return c.xy / c.z;
}

// Output color swizzle macro. Defined by ModularProgramBuilder based on the render target's
// pixel format (BGRA-vs-RGBA) and color space. If TGFX_OUT_SWIZZLE is not defined, no swizzle
// is applied.
#ifndef TGFX_OUT_SWIZZLE
#define TGFX_OUT_SWIZZLE rgba
#endif

/**
 * Applies the output swizzle to a vec4 color. The swizzle components (rgba/bgra/...) are
 * selected via the TGFX_OUT_SWIZZLE macro.
 */
vec4 TGFX_OutputSwizzle(vec4 color) {
    return color.TGFX_OUT_SWIZZLE;
}

