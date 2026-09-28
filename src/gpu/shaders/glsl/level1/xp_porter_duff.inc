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
  // Zero-coverage discard, identical to GLSLPorterDuffXferProcessor::emitCode, which emits it
  // whenever a dst texture sampler is bound. It is not optional: the dst texture is either a copy
  // of the target taken before the draw or the target itself read without a barrier between the
  // draw's own primitives, so a zero-coverage fragment that wrote `dstColor` back would replace
  // what an earlier primitive of the same draw already blended there with the pre-draw value.
  // Overlapping glyph quads hit this constantly (text_shape, emojiTextStrokeBlending lost whole
  // strokes); any overlapping batch with a dst-reading blend mode does too.
  //
  // This helper once dropped the discard, citing an Apple desktop GL miscompile ("fragments die
  // even when the condition never fires"). The miscompile is real but narrower than that: it
  // reproduced only in pointwise_chain.frag, whose coverage was read back after the evaluation
  // loop as chainResults[<uniform>]. With that read-back, a discard on the coverage killed every
  // fragment of the draw, while the identical expression selected the correct colour when used
  // without a discard. Capturing the registers inside the loop (see pointwise_chain.frag) makes
  // the discard behave; the driver-internal cause was not established. No other kernel routed
  // here reads its coverage from a uniform-indexed array.
  if (coverage.r <= 0.0 && coverage.g <= 0.0 && coverage.b <= 0.0) {
    discard;
  }
  highp vec2 dstCoord = (gl_FragCoord.xy - DstTextureUpperLeft) * DstTextureCoordScale;
  vec4 dstColor = texture(DstTextureSampler, dstCoord);
  return xpBlendWithCoverage(srcColor, dstColor, coverage, XPBlendMode);
}
#endif

#endif
