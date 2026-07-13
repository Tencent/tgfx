// ComplexNonAARRectFillShader fragment shader
// Processor layout: ComplexNonAARRectGeometryProcessor() + EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (injected as #define 0/1):
//   HAS_COMMON_COLOR: whether a common color uniform is used
//   STROKE: whether stroke mode is enabled
//   HAS_XP: 0=passthrough, 1=PorterDuff XP (dst texture blend)
// Per-corner independent radii SDF evaluation.
#version 450

#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

#if HAS_COMMON_COLOR || HAS_XP
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
#include "xp_uniforms.inc"
};
#endif

layout(location = 0) in vec2 vLocalCoord;
layout(location = 1) in vec4 vXRadii;
layout(location = 2) in vec4 vYRadii;
layout(location = 3) in vec4 vRectBounds;
#if !HAS_COMMON_COLOR
layout(location = 4) in vec4 vColor;
#if STROKE
layout(location = 5) in vec2 vStrokeWidth;
#endif
#else
#if STROKE
layout(location = 4) in vec2 vStrokeWidth;
#endif
#endif

#define XP_DST_TEX_BINDING 0
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

void main() {
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  // Per-corner radii: xR/yR order is [TL, TR, BR, BL]
  highp vec4 cornersX = vec4(vRectBounds.x, vRectBounds.z, vRectBounds.z, vRectBounds.x);
  highp vec4 cornersY = vec4(vRectBounds.y, vRectBounds.y, vRectBounds.w, vRectBounds.w);
  highp vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);
  highp vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);
  highp vec4 dx = (vec4(vLocalCoord.x) - cornersX) * signsX;
  highp vec4 dy = (vec4(vLocalCoord.y) - cornersY) * signsY;
  highp vec4 inBox = step(dx, vXRadii) * step(dy, vYRadii);
  highp vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, inBox.x + inBox.y + inBox.z);
  inBox *= step(prefixSum, vec4(0.5));
  highp float inAnyCorner = dot(inBox, vec4(1.0));

  highp vec4 arcCentersX = cornersX + signsX * vXRadii;
  highp vec4 arcCentersY = cornersY + signsY * vYRadii;
  highp vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));
  highp vec2 radii = max(vec2(dot(inBox, vXRadii), dot(inBox, vYRadii)), vec2(1e-6));

  highp vec2 ellipseOffset = (vLocalCoord - arcCenter) / radii;
  highp float insideEllipse = step(dot(ellipseOffset, ellipseOffset), 1.0);
  highp float outerCoverage = mix(1.0, insideEllipse, inAnyCorner);

#if STROKE
  highp vec2 sw = vStrokeWidth;
  highp vec2 innerRadii = max(radii - 2.0 * sw, vec2(1e-6));
  highp vec2 innerEllipseOffset = (vLocalCoord - arcCenter) / innerRadii;
  highp float insideInnerEllipse = step(dot(innerEllipseOffset, innerEllipseOffset), 1.0);
  highp vec2 center = (vRectBounds.xy + vRectBounds.zw) * 0.5;
  highp vec2 halfSize = (vRectBounds.zw - vRectBounds.xy) * 0.5;
  highp vec2 innerHalfSize = halfSize - 2.0 * sw;
  highp vec2 p = abs(vLocalCoord - center);
  highp float insideInnerRect = step(0.0, innerHalfSize.x) * step(0.0, innerHalfSize.y)
                               * step(p.x, innerHalfSize.x) * step(p.y, innerHalfSize.y);
  highp float innerCoverage = mix(insideInnerRect, insideInnerEllipse, inAnyCorner);
  highp float coverage = outerCoverage * (1.0 - innerCoverage);
#else
  highp float coverage = outerCoverage;
#endif

#define TGFX_XP_SRC_COLOR (outputColor * coverage)
#include "xp_output.inc"
}
