// ConstColorShader fragment shader
// Permutation dimensions (frag): INPUT_MODE (0=Ignore, 1=ModulateRGBA, 2=ModulateA), HAS_XP
#version 450

#ifndef INPUT_MODE
#define INPUT_MODE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 ConstColor;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

void main() {
  vec4 color = ConstColor;

#if INPUT_MODE == 1
  // ModulateRGBA: multiply by input color from previous stage
  color *= Color;
#elif INPUT_MODE == 2
  // ModulateA: multiply by input alpha only
  color *= Color.a;
#endif

#if HAS_XP
  fragColor = applyPorterDuffXP(color, vec4(1.0));
#else
  fragColor = color;
#endif
}
