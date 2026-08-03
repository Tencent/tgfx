// PorterDuff XferProcessor support (DST_TEX mode + shared blend function).
// Include this file after the FragmentUniformBlock and sampler declarations.
// Requires:
//   HAS_XP defined as 0, 1, or 2
//   XP_DST_TEX_BINDING defined to the dst texture sampler binding index (for HAS_XP==1)
//   DstTextureUpperLeft, DstTextureCoordScale, XPBlendMode declared in FragmentUniformBlock
// When HAS_XP>=1, provides the xpBlendColors() helper.
// When HAS_XP==1, also provides applyPorterDuffXP() (DST_TEX sampler path).

#if HAS_XP >= 1

#if HAS_XP == 1
#ifndef XP_DST_TEX_BINDING
#define XP_DST_TEX_BINDING 1
#endif

layout(set = 1, binding = XP_DST_TEX_BINDING) uniform sampler2D DstTextureSampler;
#endif

#include "xp_blend_colors.inc"

#if HAS_XP == 1
vec4 applyPorterDuffXP(vec4 srcColor, vec4 coverage) {
  if (coverage.r <= 0.0 && coverage.g <= 0.0 && coverage.b <= 0.0) {
    discard;
  }
  highp vec2 dstCoord = (gl_FragCoord.xy - DstTextureUpperLeft) * DstTextureCoordScale;
  vec4 dstColor = texture(DstTextureSampler, dstCoord);
  vec4 blended = xpBlendColors(srcColor, dstColor, XPBlendMode);
  return coverage * blended + (vec4(1.0) - coverage) * dstColor;
}
#endif

#endif
