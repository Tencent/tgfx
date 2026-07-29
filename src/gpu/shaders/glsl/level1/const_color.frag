// ConstColorShader fragment shader
// Permutation dimensions (frag): HAS_XP
// INPUT_MODE (0=Ignore, 1=ModulateRGBA, 2=ModulateA) is a runtime uniform (InputMode), not a
// compile-time permutation: it is pure fragment math, so folding it into a uniform branch shrinks
// the variant count. GLSLConstColorProcessor::onSetData writes InputMode (guarded by hasField).
#version 450

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
  int InputMode;
};

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

void main() {
  vec4 color = ConstColor;

  if (InputMode == 1) {
    // ModulateRGBA: multiply by input color from previous stage
    color *= Color;
  } else if (InputMode == 2) {
    // ModulateA: multiply by input alpha only
    color *= Color.a;
  }

#if HAS_XP
  fragColor = applyPorterDuffXP(color, vec4(1.0));
#else
  fragColor = color;
#endif
}
