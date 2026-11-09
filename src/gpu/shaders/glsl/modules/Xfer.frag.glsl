// Xfer fragment hook: the composition site. The expressions are verbatim from the former
// xp/xp_output.inc and xp/xp_porter_duff.inc routes: HAS_XP = 0 writes the coverage-premultiplied
// source, HAS_XP = 1 reads the dst texture (with the zero-coverage discard), HAS_XP = 2 reads the
// framebuffer through the subpass input. The blend helpers define xpBlendWithCoverage only when
// HAS_XP is not 0.
#include "xp/xp_blend_colors.inc"

#if HAS_XP == 2
layout(input_attachment_index = 0, set = TGFX_FBF_INPUT_SET, binding = 0) uniform subpassInput tgfx_SubpassInput;
#endif

void Xfer_fragment(inout Fragment f) {
#if HAS_XP == 2
  vec4 dstColor = subpassLoad(tgfx_SubpassInput);
  fragColor = xpBlendWithCoverage(f.color, dstColor, f.coverage, XPBlendMode);
#elif HAS_XP == 1
  // The zero-coverage discard is not optional: without it a zero-coverage fragment writes the
  // pre-draw destination back over what an earlier primitive of the same draw already blended
  // there (see xp/xp_porter_duff.inc for the full history).
  if (f.coverage.r <= 0.0 && f.coverage.g <= 0.0 && f.coverage.b <= 0.0) {
    discard;
  }
  highp vec2 dstCoord = (gl_FragCoord.xy - DstTextureUpperLeft) * DstTextureCoordScale;
  vec4 dstColor = texture(DstTextureSampler, dstCoord);
  fragColor = xpBlendWithCoverage(f.color, dstColor, f.coverage, XPBlendMode);
#else
  fragColor = f.color * f.coverage;
#endif
}
