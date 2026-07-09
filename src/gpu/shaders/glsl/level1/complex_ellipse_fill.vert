// ComplexEllipseFillShader vertex shader
// Processor layout: ComplexEllipseGeometryProcessor(_P0) + EmptyXferProcessor(_P1)
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled
//   HAS_COMMON_COLOR: whether a common color uniform is used
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif

layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  vec4 tgfx_RTAdjust;
};

layout(location = 0) in vec2 inPosition;
#if !HAS_COMMON_COLOR
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inEllipseOffset;
layout(location = 3) in vec4 inEllipseRadii;
layout(location = 4) in vec2 inEdgeDist;
#if STROKE
layout(location = 5) in vec2 inStrokeWidth;
#endif
#else
layout(location = 1) in vec2 inEllipseOffset;
layout(location = 2) in vec4 inEllipseRadii;
layout(location = 3) in vec2 inEdgeDist;
#if STROKE
layout(location = 4) in vec2 inStrokeWidth;
#endif
#endif

layout(location = 0) out vec2 vEllipseOffsets;
layout(location = 1) out vec4 vEllipseRadii;
layout(location = 2) out vec2 vEdgeDist;
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
  vEllipseOffsets = inEllipseOffset;
  vEllipseRadii = inEllipseRadii;
  vEdgeDist = inEdgeDist;
#if !HAS_COMMON_COLOR
  vColor = inColor;
#endif
#if STROKE
  vStrokeWidth = inStrokeWidth;
#endif
  gl_Position = vec4(inPosition.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0.0, 1.0);
}
