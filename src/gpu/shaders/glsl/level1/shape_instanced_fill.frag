// ShapeInstancedFillShader fragment shader
// Processor layout: ShapeInstancedGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_COLORS: whether per-instance colors are used
//   HAS_AA: whether coverage-based AA is enabled
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
#version 450

#ifndef HAS_COLORS
#define HAS_COLORS 0
#endif
#ifndef HAS_AA
#define HAS_AA 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_AA
layout(location = 1) in float vCoverage;
#endif
#if HAS_COLORS
layout(location = 2) in vec4 vColor;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLORS
  vec4 outputColor = vColor;
#else
  vec4 outputColor = vec4(1.0);
#endif

#if HAS_AA
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

#define TGFX_XP_SRC_COLOR (outputColor * outputCoverage)
#include "xp_output.inc"
}
