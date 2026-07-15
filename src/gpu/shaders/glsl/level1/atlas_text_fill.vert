// AtlasTextFillShader vertex shader
// Processor layout: AtlasTextGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_COVERAGE, HAS_COMMON_COLOR, ALPHA_ONLY
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  vec2 atlasSizeInv;
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
layout(location = 2) in vec2 maskCoord;
#if !HAS_COMMON_COLOR
layout(location = 3) in vec4 inColor;
#endif
#else
layout(location = 1) in vec2 maskCoord;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 inColor;
#endif
#endif

layout(location = 0) out vec2 vTextureCoords;
#if HAS_COVERAGE
layout(location = 1) out float vCoverage;
#if !HAS_COMMON_COLOR
layout(location = 2) out vec4 vColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 1) out vec4 vColor;
#endif
#endif

void main() {
  vTextureCoords = maskCoord * atlasSizeInv;
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
#if !HAS_COMMON_COLOR
  vColor = inColor;
#endif
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
