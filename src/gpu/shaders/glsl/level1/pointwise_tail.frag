// PointwiseTailShader fragment shader.
// SourceKind is a runtime field: 0 samples the local TextureEffect coordinate and 1 samples a
// DeviceSpaceTextureEffect from gl_FragCoord. The two operator slots are per-slot uniform arrays
// (capacity 2), each carrying the full parameter set of every pointwise operator; a single
// applyPointwiseOp instance serves both slots through the runtime-bound slot loop, so the operator
// library exists exactly once in the binary.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int SourceKind;
  mat3 DeviceCoordMatrix;
  // Always declared: a draw without a subset uploads the full texture bounds, making the clamp a
  // no-op, so subset handling needs no compile-time dimension.
  vec4 Subset;

int PointwiseSlotCount;

#define TGFX_SLOT_ARRAY_SUFFIX [2]
#include "pointwise_op_uniforms.inc"
#undef TGFX_SLOT_ARRAY_SUFFIX

#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_slot_array_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_array_unbind.inc"

void main() {
  highp vec2 finalCoord = TransformedCoords_0;
  if (SourceKind != 0) {
    finalCoord = (DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0)).xy;
  }
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
  vec4 color = texture(TextureSampler_0, finalCoord) * Color.a;
  vec4 result = color;
  // The loop bound is a runtime uniform so the compiler cannot unroll and duplicate the operator
  // library; inactive slots carry OP_NONE and pass the color through unchanged.
  for (int i = 0; i < PointwiseSlotCount; ++i) {
    result = applyPointwiseOp(i, result);
  }

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
