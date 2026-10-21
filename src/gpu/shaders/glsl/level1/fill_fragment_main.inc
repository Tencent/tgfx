// Fragment stage of the dedicated kernels: one straight-line pipeline shared by every dedicated
// family. The pointwise chain is the other pipeline (pointwise_fragment_main.inc); both call the
// same texture, coverage, clip and XferProcessor modules, so each formula exists once.
//
//   1. color      FILL_COLOR          where the input color comes from
//   2. source     FILL_SOURCE         the input color, or a texture modulated by it
//   3. operator   FILL_OPERATOR       optional single pointwise operator (pointwise_op.inc)
//   4. coverage   FILL_COVERAGE       the geometry's own coverage, then the optional device mask
//                 FILL_DEVICE_MASK    (FILL_DEVICE_MASK), local mask (FILL_LOCAL_MASK) and clip
//                 FILL_LOCAL_MASK     (FILL_CLIP), always multiplied in this order
//                 FILL_CLIP
//   5. output     xp_output.inc, then the alpha swizzle when FILL_OUTPUT_SWIZZLE
//
// A family entry declares its interface (uniform block, varyings, samplers, XP_DST_TEX_BINDING) in
// its runtime binding order, selects the steps, and includes this file last. Every selector is a
// compile-time constant, so each family compiles to its own direct program with no dispatch.

#define FILL_COLOR_UNIFORM 0            // Color uniform
#define FILL_COLOR_VARYING 1            // vColor varying
#define FILL_COLOR_VARYING_OR_COMMON 2  // vColor, or the exact Color uniform when HasCommonColor != 0
                                        // (the varying is 8-bit quantized)

#define FILL_SOURCE_COLOR 0  // the input color
// TextureEffect contract: TextureSampler_0 at TransformedCoords_0 (vec3, perspective divided),
// clamped to the per-quad vTexSubset varying when HAS_SUBSET, then to Subset when
// SubsetClampOn != 0, decoded through AlphaOnly / HasRgbaaa / AlphaStart, modulated by the color.
#define FILL_SOURCE_TEXTURE 1
// TextureSampler_0 at TransformedCoords_0 (vec2) clamped to Subset, times the color's alpha.
#define FILL_SOURCE_CLAMPED_TEXTURE 2

#define FILL_COVERAGE_NONE 0
#define FILL_COVERAGE_VERTEX 1   // vCoverage varying
#define FILL_COVERAGE_ELLIPSE 2  // ellipse_coverage.inc on vEllipseOffsets / vEllipseRadii
#define FILL_COVERAGE_MASK 3     // red channel of TextureSampler_0 at TransformedCoords_0
#define FILL_COVERAGE_HAIRLINE_LINE 4  // hairline_line_coverage.inc on vEdgeDistance
#define FILL_COVERAGE_HAIRLINE_QUAD 5  // hairline_quad_coverage.inc (Loop-Blinn) on vHairQuadEdge

#define FILL_DEVICE_MASK_NONE 0
#define FILL_DEVICE_MASK_ALWAYS 1   // MaskTextureSampler through DeviceCoordMatrix
#define FILL_DEVICE_MASK_RUNTIME 2  // the same, when HasDeviceMask != 0

#ifndef FILL_COLOR
#define FILL_COLOR FILL_COLOR_UNIFORM
#endif
#ifndef FILL_SOURCE
#define FILL_SOURCE FILL_SOURCE_COLOR
#endif
#ifndef FILL_OPERATOR
#define FILL_OPERATOR 0
#endif
#ifndef FILL_COVERAGE
#define FILL_COVERAGE FILL_COVERAGE_NONE
#endif
#ifndef FILL_DEVICE_MASK
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_NONE
#endif
#ifndef FILL_LOCAL_MASK
#define FILL_LOCAL_MASK 0
#endif
#ifndef FILL_CLIP
#define FILL_CLIP 0
#endif
#ifndef FILL_OUTPUT_SWIZZLE
#define FILL_OUTPUT_SWIZZLE 0
#endif

#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#if FILL_OPERATOR
#include "xp_blend_pass.inc"
#endif
#if FILL_SOURCE != FILL_SOURCE_COLOR
#include "texture_sampling.inc"
#endif
#if FILL_COVERAGE == FILL_COVERAGE_ELLIPSE
#include "ellipse_coverage.inc"
#endif
#if FILL_COVERAGE == FILL_COVERAGE_HAIRLINE_LINE
#include "hairline_line_coverage.inc"
#endif
#if FILL_COVERAGE == FILL_COVERAGE_HAIRLINE_QUAD
#include "hairline_quad_coverage.inc"
#endif
#if FILL_CLIP
#include "clip_coverage.inc"
#endif

layout(location = 0) out vec4 fragColor;

#if FILL_OPERATOR
#include "pointwise_op.inc"
#endif

#if FILL_DEVICE_MASK != FILL_DEVICE_MASK_NONE
float fillDeviceMaskCoverage() {
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  return texture(MaskTextureSampler, maskCoord.xy).r;
}
#endif

void main() {
#if FILL_COLOR == FILL_COLOR_UNIFORM
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#if FILL_COLOR == FILL_COLOR_VARYING_OR_COMMON
  if (HasCommonColor != 0) {
    outputColor = Color;
  }
#endif
#endif

#if FILL_SOURCE == FILL_SOURCE_TEXTURE
  highp vec2 finalCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
#if HAS_SUBSET
  finalCoord = aotTextureClamp(finalCoord, vTexSubset, true);
#endif
  // The Subset clamp follows the runtime TextureEffect's needSubset() gate (SubsetClampOn): an
  // ungated clamp fires on out-of-range coordinates (perspective extrapolation, edge quads),
  // zeroing the mip derivatives and diverging from the runtime codegen.
  vec4 color = aotTextureSource(TextureSampler_0, finalCoord, outputColor, Subset, SubsetClampOn,
                                AlphaStart, AlphaOnly, HasRgbaaa, 0);
#elif FILL_SOURCE == FILL_SOURCE_CLAMPED_TEXTURE
  vec4 color = aotTextureSource(TextureSampler_0, TransformedCoords_0, outputColor, Subset, 1,
                                vec2(0.0), 0, 0, 0);
#else
  vec4 color = outputColor;
#endif

#if FILL_OPERATOR && HAS_XP
  // A dst-reading XferProcessor in an advanced mode runs its blend as one more pass of the
  // operator loop, so xpBlendColors is inlined once (see xp_blend_pass.inc).
  vec4 xpDst = xpReadDstColor();
  vec4 xpBlended = vec4(0.0);
  vec4 result = color;
  int passCount = xpNeedsBlendPass() ? 2 : 1;
  for (int pass = 0; pass < passCount; ++pass) {
    bool xpPass = pass == 1;
    vec4 value = applyPointwiseOp(0, result, xpPass, xpDst);
    if (xpPass) {
      xpBlended = value;
    } else {
      result = value;
    }
  }
#define TGFX_XP_DST_COLOR xpDst
#define TGFX_XP_BLENDED xpBlended
#elif FILL_OPERATOR
  vec4 result = applyPointwiseOp(0, color);
#else
  vec4 result = color;
#endif

#if FILL_COVERAGE == FILL_COVERAGE_VERTEX
  float totalCoverage = vCoverage;
#elif FILL_COVERAGE == FILL_COVERAGE_ELLIPSE
  float totalCoverage = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);
#elif FILL_COVERAGE == FILL_COVERAGE_MASK
  float totalCoverage = texture(TextureSampler_0, TransformedCoords_0).r;
#elif FILL_COVERAGE == FILL_COVERAGE_HAIRLINE_LINE
  float totalCoverage = hairlineLineEdgeCoverage(vEdgeDistance, Coverage, AAEnabled);
#elif FILL_COVERAGE == FILL_COVERAGE_HAIRLINE_QUAD
  float totalCoverage = hairlineQuadEdgeCoverage(vHairQuadEdge, Coverage, AAEnabled);
#else
  float totalCoverage = 1.0;
#endif
#if FILL_DEVICE_MASK == FILL_DEVICE_MASK_RUNTIME
  if (HasDeviceMask != 0) {
    totalCoverage *= fillDeviceMaskCoverage();
  }
#elif FILL_DEVICE_MASK == FILL_DEVICE_MASK_ALWAYS
  totalCoverage *= fillDeviceMaskCoverage();
#endif
#if FILL_LOCAL_MASK
  highp vec2 localMaskCoord = TransformedCoords_1.xy / TransformedCoords_1.z;
  totalCoverage *= texture(LocalMaskSampler, localMaskCoord).a;
#endif
#if FILL_CLIP
  totalCoverage *= clipCoverage();
#endif

#define TGFX_XP_SRC_COLOR (result * totalCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(totalCoverage)
#include "xp_output.inc"
#if FILL_OUTPUT_SWIZZLE
#include "output_swizzle.inc"
#endif
}
