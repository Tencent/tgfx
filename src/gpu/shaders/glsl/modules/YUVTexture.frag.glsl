// YUVTexture fragment hook: the three-plane read, the YUV-to-RGB conversion and the bounded
// pointwise tail the legacy kernel carried (the same slot encoding, the same operator table and
// the same XP composite as the chain interpreter, at the tail's own capacity). The expressions
// are verbatim from the old kernel's main; the field names are the declared ones (the tail module
// keeps the legacy bare spellings so the shared slot loop and the clip helper bind unchanged).

// The declared assembly declares the dst sampler among the Xfer module's elements, so the shared
// xp include must skip its own legacy declaration (the same switch the chain assembly sets).
#define TGFX_DECLARED_CHAIN 1

#pragma tgfx_chain_ops

#include "coverage/clip_coverage.inc"
#include "pipeline/chain/pointwise_xp_support.inc"
#include "pipeline/slot/pointwise_op.inc"
#include "pipeline/slot/pointwise_slot_loop.inc"

void YUVTexture_fragment(inout Fragment f) {
  highp vec2 texCoord = YUVTexture_vCoord.xy / YUVTexture_vCoord.z;
  highp vec2 finalCoord = clamp(texCoord, YUVTexture_Subset.xy, YUVTexture_Subset.zw);

  vec3 yuv;
  yuv.x = texture(YUVTexture_SamplerY, finalCoord).r;
#if YUVTexture_FORMAT == 0
  yuv.y = texture(YUVTexture_SamplerUV, finalCoord).r;
  yuv.z = texture(YUVTexture_SamplerV, finalCoord).r;
#else
  // The UV plane is an RG_88 texture; .rg reads (U, V) directly on every backend's plain binding.
  yuv.yz = texture(YUVTexture_SamplerUV, finalCoord).rg;
#endif
  if (YUVTexture_LimitedRange != 0) {
    yuv.x -= (16.0 / 255.0);
  }
  yuv.yz -= vec2(0.5, 0.5);
  vec3 rgb = clamp(YUVTexture_ColorConversion * yuv, 0.0, 1.0);
  vec4 color;
  if (YUVTexture_HasRgbaaa != 0) {
    highp vec2 alphaVertexColor = finalCoord + YUVTexture_AlphaStart;
    float yuv_a = texture(YUVTexture_SamplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    color = vec4(rgb * yuv_a, yuv_a);
  } else {
    color = vec4(rgb, 1.0);
  }
  color = color * f.color.a;

  // The tail: operators apply after the plane readback, including the paint-alpha multiply above
  // (the runtime Compose order). The XP advanced blend runs as the tail's last pass.
#if HAS_XP >= 1
  vec4 xpDst = xpReadDstColor();
  vec4 xpBlended;
  vec4 result = applyPointwiseSlots(color, xpDst, xpBlended);
#else
  vec4 result = applyPointwiseSlots(color);
#endif

  // The body owns the output: the geometry's coverage folded with the analytic clip, then the
  // XP composite (the Xfer module contributes the interface but not its hook).
  float totalCoverage = f.coverage.r * clipCoverage();
#if HAS_XP >= 1
  fragColor = xpCompositeBlendPass(result, xpDst, vec4(totalCoverage), xpBlended);
#else
  fragColor = result * totalCoverage;
#endif
#include "xp/output_swizzle.inc"
}
