// TexturedEffectShader vertex shader
// Shared by the (S1, none, {noXP,XP}, RGBA) cold uber: samples an intermediate texture then applies
// one runtime-selected pointwise operator. Identical to the former textured_{color_matrix,luma,
// alpha_threshold,color_space_xform}.vert (they differed only in comments).
// The coverage attribute is unconditional: the non-AA path triangles carry a constant 1.0 slot
// (they share the AA vertex layout), so the kernel binds it in a single variant and the non-AA
// modulation by one is the identity.
#version 450

layout(std140, set = TGFX_VERTEX_UBO_SET, binding = TGFX_VERTEX_UBO_BINDING) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
  mat3 Matrix;
  mat3 CoordTransformMatrix_0;
};

layout(location = 0) in vec2 aPosition;
layout(location = 1) in float inCoverage;
// The color attribute is unconditional on both GPs this family accepts (the DefaultGP and the
// QuadPerEdgeAAGP): the shared vertex buffers always carry a color slot, and the exact paint color
// rides the Color uniform through HasCommonColor. Declaring it keeps the reflected vertex interface
// identical to the GP's attribute list, which the program creator compares by count.
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 TransformedCoords_0;
layout(location = 1) out float vCoverage;
layout(location = 2) out vec4 vColor;

void main() {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(aPosition, 1.0)).xy;
  vCoverage = inCoverage;
  vColor = inColor;
  gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
