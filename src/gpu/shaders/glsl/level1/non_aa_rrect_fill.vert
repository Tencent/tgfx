// NonAARRectFillShader vertex shader
// Processor layout: NonAARRectGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled (adds stroke width attribute)
//   TEXTURED: whether a texture child rides the fill
// The color attribute is unconditional: common-color batches broadcast the paint color into the
// slot (an 8-bit quantized filler), and the exact common color rides the fragment stage's Color
// uniform, selected by the HasCommonColor runtime uniform.
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef TEXTURED
#define TEXTURED 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
#if TEXTURED
  mat3 CoordTransformMatrix_0;
#endif
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inLocalCoord;
layout(location = 2) in vec2 inRadii;
layout(location = 3) in vec4 inRectBounds;
layout(location = 4) in vec4 inColor;
#if STROKE
layout(location = 5) in vec2 inStrokeWidth;
#endif

layout(location = 0) out vec2 vLocalCoord;
layout(location = 1) out vec2 vRadii;
layout(location = 2) out vec4 vRectBounds;
layout(location = 3) out vec4 vColor;
#if TEXTURED
// Texture coordinates come from the local coordinate through the FP's transform, always at a
// fixed location beyond every attribute-driven varying.
layout(location = 5) out vec2 TransformedCoords_0;
#endif
#if STROKE
layout(location = 4) out vec2 vStrokeWidth;
#endif

void main() {
#if TEXTURED
  TransformedCoords_0 = (CoordTransformMatrix_0 * vec3(inLocalCoord, 1.0)).xy;
#endif
  vLocalCoord = inLocalCoord;
  vRadii = inRadii;
  vRectBounds = inRectBounds;
  vColor = inColor;
#if STROKE
  vStrokeWidth = inStrokeWidth;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
