// MeshFillShader fragment shader
// Processor layout: MeshGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_TEX_COORDS: whether user-provided texture coordinates are present
//   HAS_COLOR: whether per-vertex colors are present
//   HAS_COVERAGE: whether per-vertex coverage is present
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
#version 450

#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if !HAS_COLOR || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if !HAS_COLOR
  vec4 Color;
#endif
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COLOR
layout(location = 1) in vec4 vColor;
#endif
#if HAS_COVERAGE
layout(location = 2) in float vCoverage;
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COLOR
  vec4 outputColor = vColor;
#else
  vec4 outputColor = Color;
#endif

#if HAS_COVERAGE
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

// The XP path needs the uncovered color plus the total coverage separately; defining only the
// covered SRC_COLOR double-applies the coverage inside the XferProcessor blend.
#define TGFX_XP_SRC_COLOR (outputColor * outputCoverage)
#define TGFX_XP_SRC_UNPREMUL outputColor
#define TGFX_XP_COVERAGE outputCoverage
#include "xp_output.inc"
}
