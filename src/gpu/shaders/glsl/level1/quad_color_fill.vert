// QuadColorFillShader vertex shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor (no FP)
// No permutation dimensions remain. Both optional vertex inputs are unconditional: providers emit
// coverage=1.0 for non-AA draws and broadcast the record's paint color for uniform-color batches,
// so vCoverage and vColor are the only data paths.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;
layout(location = 2) in vec4 inColor;

layout(location = 0) out float vCoverage;
layout(location = 1) out vec4 vColor;

void main() {
  vCoverage = inCoverage;
  vColor = inColor;

  gl_Position = vec4(aPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
