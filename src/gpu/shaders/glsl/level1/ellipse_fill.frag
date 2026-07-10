// EllipseFillShader fragment shader
// Processor layout: EllipseGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled (inner curve check)
//   HAS_COMMON_COLOR: whether a common color uniform is used
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif

#if HAS_COMMON_COLOR
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
};
#endif

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  // Outer curve coverage using ellipse distance approximation
  highp vec2 offset = vEllipseOffsets;
#if STROKE
  offset *= vEllipseRadii.xy;
#endif
  highp float test = dot(offset, offset) - 1.0;
  highp vec2 grad = 2.0 * offset * vEllipseRadii.xy;
  highp float gradDot = dot(grad, grad);
  gradDot = max(gradDot, 1.1755e-38);
  highp float invlen = inversesqrt(gradDot);
  highp float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);

#if STROKE
  // Inner curve coverage for stroke
  highp vec2 innerOffset = vEllipseOffsets * vEllipseRadii.zw;
  highp float innerTest = dot(innerOffset, innerOffset) - 1.0;
  highp vec2 innerGrad = 2.0 * innerOffset * vEllipseRadii.zw;
  highp float innerGradDot = dot(innerGrad, innerGrad);
  innerGradDot = max(innerGradDot, 1.1755e-38);
  highp float innerInvlen = inversesqrt(innerGradDot);
  edgeAlpha *= clamp(0.5 + innerTest * innerInvlen, 0.0, 1.0);
#endif

  fragColor = outputColor * edgeAlpha;
}
