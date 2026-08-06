// QuadConstColorShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + ConstColorProcessor + EmptyXferProcessor
// Permutation dimensions: HAS_UV_COORD
#version 450

#ifndef HAS_UV_COORD
#define HAS_UV_COORD 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;

#if HAS_UV_COORD
layout(location = 2) in vec2 uvCoord;
layout(location = 3) in vec4 inColor;
#else
layout(location = 2) in vec4 inColor;
#endif

layout(location = 0) out float vCoverage;
layout(location = 1) out vec4 vColor;

void main() {
  vCoverage = inCoverage;
  vColor = inColor;
  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
