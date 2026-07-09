// NonAARRectFillShader vertex shader
// Processor layout: NonAARRectGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   STROKE: whether stroke mode is enabled (adds stroke width attribute)
#version 450

#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef STROKE
#define STROKE 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inLocalCoord;
layout(location = 2) in vec2 inRadii;
layout(location = 3) in vec4 inRectBounds;
#if !HAS_COMMON_COLOR
layout(location = 4) in vec4 inColor;
#if STROKE
layout(location = 5) in vec2 inStrokeWidth;
#endif
#else
#if STROKE
layout(location = 4) in vec2 inStrokeWidth;
#endif
#endif

layout(location = 0) out vec2 vLocalCoord;
layout(location = 1) out vec2 vRadii;
layout(location = 2) out vec4 vRectBounds;
#if !HAS_COMMON_COLOR
layout(location = 3) out vec4 vColor;
#if STROKE
layout(location = 4) out vec2 vStrokeWidth;
#endif
#else
#if STROKE
layout(location = 3) out vec2 vStrokeWidth;
#endif
#endif

void main() {
  vLocalCoord = inLocalCoord;
  vRadii = inRadii;
  vRectBounds = inRectBounds;
#if !HAS_COMMON_COLOR
  vColor = inColor;
#endif
#if STROKE
  vStrokeWidth = inStrokeWidth;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
