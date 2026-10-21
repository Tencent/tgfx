// AtlasTextFillShader vertex shader
// Processor layout: AtlasTextGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_COVERAGE
// The color attribute is unconditional: common-color glyph batches broadcast the paint color
// into the slot (a single variant), and the exact common color rides the Color uniform through
// the HasCommonColor runtime uniform (the broadcast is 8-bit quantized).
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  vec2 atlasSizeInv;
};

layout(location = 0) in vec2 aPosition;
#if HAS_COVERAGE
layout(location = 1) in float inCoverage;
layout(location = 2) in vec2 maskCoord;
layout(location = 3) in vec4 inColor;
#else
layout(location = 1) in vec2 maskCoord;
layout(location = 2) in vec4 inColor;
#endif

layout(location = 0) out vec2 vTextureCoords;
#if HAS_COVERAGE
layout(location = 1) out float vCoverage;
layout(location = 2) out vec4 vColor;
#else
layout(location = 1) out vec4 vColor;
#endif

void main() {
  vTextureCoords = maskCoord * atlasSizeInv;
#if HAS_COVERAGE
  vCoverage = inCoverage;
#endif
  vColor = inColor;
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
