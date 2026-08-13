// PointwiseDirectShader fragment shader.
// Applies one runtime-selected pointwise operator directly to the input color, with no texture
// source. Merges the former LumaShader / AlphaThresholdShader / ColorSpaceXformShader, whose
// skeletons were identical and differed only in the operator: the operator is now the OpType runtime
// uniform from the shared pointwise operator table, not three separate shaders.
//
// This is the source-less sibling of pointwise_tail.frag. The two stay separate kernels because the
// varying layout differs — without a texture source there is no TransformedCoords_0, so vCoverage
// occupies location 0 — and the vertex/fragment interface is a structural axis, not a parameter.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#include "pointwise_op_uniforms.inc"
#include "xp_uniforms.inc"
};

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_op.inc"

void main() {
  vec4 result = applyPointwiseOp(0, Color);

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
