// GlassRefractionShader vertex shader
// Processor layout: EllipseGeometryProcessor(common color) or QuadPerEdgeAAGeometryProcessor
// (common color, uvMatrix, per-edge coverage) + GlassRefractionFragmentProcessor.
// Permutation dimensions (vert):
//   GP_KIND (0~1): 0=ellipse analytic attributes, 1=quad per-edge coverage + position transform
#version 450

#ifndef GP_KIND
#define GP_KIND 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 CoordTransformMatrix_0;
};

#if GP_KIND == 1

// The quad layout is (position, coverage, color): the matcher only accepts the uvMatrix form of
// QuadPerEdgeAAGeometryProcessor, and that form declares NO uvCoord attribute — the GP sources
// its coordinate transform from the position instead (GLSLQuadPerEdgeAAGeometryProcessor::
// emitCode picks `uvCoord.empty() ? position : uvCoord`). Declaring a uvCoord here would bind
// location 2, which in this form carries inColor, and feed the paint colour into the background
// transform. The color attribute stays undeclared because the matcher also requires the
// common-color form, so the fragment reads the Color uniform instead.
layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;

// Affine coordinate varying, matching GeometryProcessor::emitTransforms' non-perspective path:
// it computes `(Matrix * vec3(pos, 1)).xy` in the vertex stage and interpolates a vec2. Passing a
// vec3 and dividing by .z in the fragment stage is algebraically the same for an affine matrix but
// NOT bit-identical — the interpolated z is only approximately 1.0, so the divide shifts the
// sampled coordinate by an ULP and flips the occasional texel. The matcher rejects projective
// transforms (the same policy every other kernel here follows), so the affine form is the only
// one this kernel has to reproduce.
layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out float vCoverage;

void main() {
  vCoverage = inCoverage;
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
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
