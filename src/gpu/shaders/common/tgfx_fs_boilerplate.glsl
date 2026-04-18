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
