// SolidColorFillShader vertex shader
// Processor layout: DefaultGeometryProcessor (no fragment processors)
// The coverage attribute is unconditional: the non-AA path triangles carry a constant 1.0 slot
// (they share the AA vertex layout), so the kernel binds it in a single variant and the non-AA
// modulation by one is the identity.
#version 450

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;

layout(location = 0) out float vCoverage;

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  vCoverage = inCoverage;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
