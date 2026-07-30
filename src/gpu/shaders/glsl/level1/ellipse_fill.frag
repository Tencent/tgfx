// EllipseFillShader fragment shader
// Processor layout: EllipseGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   STROKE: whether stroke mode is enabled (inner curve check)
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
//   HAS_COVERAGE: 0=none, 1=AARect clip, 2=device-space mask texture
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

#if HAS_COMMON_COLOR || HAS_XP || HAS_COVERAGE
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif

#if HAS_COVERAGE == 2
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1
#else
#define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

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

  // Combine the ellipse's own edge antialiasing with any clip/mask coverage. HAS_COVERAGE is
  // handled inline (not via coverage_output.inc) because a bare device-space mask (type 2) carries
  // no AARect, so its Rect uniform is never uploaded — the shared include applies AARect
  // unconditionally and would multiply by a garbage rect. Each coverage type is gated independently
  // here: type 1 applies the AARect clip, type 2 samples the device-space mask.
  highp float coverage = edgeAlpha;
#if HAS_COVERAGE == 1
  highp vec4 clipDists = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
  highp vec2 clipDists2 = clipDists.xy + clipDists.zw - 1.0;
  coverage *= clipDists2.x * clipDists2.y;
#elif HAS_COVERAGE == 2
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  coverage *= texture(MaskTextureSampler, maskCoord.xy).r;
#endif

#define TGFX_XP_SRC_COLOR (outputColor * coverage)
#define TGFX_XP_SRC_UNPREMUL outputColor
#define TGFX_XP_COVERAGE vec4(coverage)
#include "xp_output.inc"
}
