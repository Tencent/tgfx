// GlassRefractionShader vertex shader
// Processor layout: EllipseGeometryProcessor(common color) or QuadPerEdgeAAGeometryProcessor
// (common color, uvCoord, per-edge coverage) + GlassRefractionFragmentProcessor.
// Permutation dimensions (vert):
//   GP_KIND (0~1): 0=ellipse analytic attributes, 1=quad per-edge coverage + uvCoord transform
#version 450

#ifndef GP_KIND
#define GP_KIND 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
};

#if GP_KIND == 1

// The quad layout mirrors the fill kernels' attribute order (position, coverage, uvCoord). The
// color attribute exists in the vertex stream but stays undeclared: the matcher requires the
// common-color form, so the fragment reads the Color uniform instead.
layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;
layout(location = 2) in vec2 uvCoord;

layout(location = 0) out vec3 TransformedCoords_0;
layout(location = 1) out float vCoverage;

void main() {
  vCoverage = inCoverage;
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = CoordTransformMatrix_0 * vec3(uvCoord, 1.0);
}

#else

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inEllipseOffset;
layout(location = 2) in vec4 inEllipseRadii;

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
layout(location = 2) out vec2 TransformedCoords_0;

void main() {
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(inPosition, 1.0)).xy;
}

#endif
