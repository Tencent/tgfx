// RoundStrokeRectFillShader vertex shader
// Processor layout: RoundStrokeRectGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_AA: whether coverage-based AA is enabled
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_UV_MATRIX: whether a UV matrix transform is used (no per-vertex UV coord)
#version 450

#ifndef HAS_AA
#define HAS_AA 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_UV_MATRIX
#define HAS_UV_MATRIX 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if HAS_UV_MATRIX
  mat3 CoordTransformMatrix_0;
#endif
};

// Attribute layout depends on permutation
layout(location = 0) in vec2 inPosition;
#if HAS_AA
layout(location = 1) in float inCoverage;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec2 inEllipseRadii;
#if !HAS_UV_MATRIX
layout(location = 4) in vec2 inUVCoord;
#if !HAS_COMMON_COLOR
layout(location = 5) in vec4 inColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 4) in vec4 inColor;
#endif
#endif
#else
layout(location = 1) in vec2 inEllipseOffset;
#if !HAS_UV_MATRIX
layout(location = 2) in vec2 inUVCoord;
#if !HAS_COMMON_COLOR
layout(location = 3) in vec4 inColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 inColor;
#endif
#endif
#endif

layout(location = 0) out vec2 vEllipseOffsets;
#if HAS_AA
layout(location = 1) out float vCoverage;
layout(location = 2) out vec2 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 3) out vec4 vColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 1) out vec4 vColor;
#endif
#endif
#if HAS_UV_MATRIX
layout(location = 6) out vec2 TransformedCoords_0;
#endif

void main() {
  vEllipseOffsets = inEllipseOffset;
#if HAS_AA
  vCoverage = inCoverage;
  vEllipseRadii = inEllipseRadii;
#endif
#if !HAS_COMMON_COLOR
  vColor = inColor;
#endif
#if HAS_UV_MATRIX
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(inPosition, 1.0)).xy;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
