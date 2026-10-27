// ShapeInstancedFillShader vertex shader
// Processor layout: ShapeInstancedGeometryProcessor() + EmptyXferProcessor()
// The instance color attribute is unconditional: colorless batches broadcast white, so every
// variant binds the color in a single form. The coverage attribute is unconditional too: the
// non-AA path triangles carry a constant 1.0 slot (they share the AA vertex layout), so the
// non-AA modulation by one is the identity.
#version 450

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 UVMatrix;
  mat3 ViewMatrix;
  mat3 CoordTransformMatrix_0;
};

// Vertex attributes
layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;
// Location 2 is the color stride filler of the shared 16-byte path-triangle layout; the kernel
// never reads it (the instance color is the color source).

// Instance attributes
layout(location = 3) in vec2 aOffset;
layout(location = 4) in vec4 aColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out float vCoverage;
layout(location = 2) out vec4 vColor;

void main() {
  // Recover local coords via UV matrix
  highp vec2 local = (UVMatrix * vec3(aPosition, 1.0)).xy;
  // Transform to device space + instance offset
  highp vec2 position = (ViewMatrix * vec3(aPosition, 1.0)).xy + aOffset;
  // Coord transform for FP sampling
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(local, 1.0)).xy;
  vCoverage = inCoverage;
  vColor = aColor;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
