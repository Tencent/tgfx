// EllipseFillShader fragment entry: EllipseGeometryProcessor + EmptyXferProcessor/PorterDuffXP.
// The pipeline is the shared dedicated one (fill_fragment_main.inc); this file declares the
// family's interface and selects the steps. The ellipse edge coverage (ellipse_coverage.inc, shared
// with the pointwise chain's GP_LAYOUT 1) is the true coverage: it reaches the XferProcessor's
// coverage input together with the device mask and the clip, never baked into the source color.
// Fragment dimensions:
//   HAS_COMMON_COLOR: the Color uniform instead of the vColor varying
//   HAS_XP: 0=passthrough, 1=PorterDuff dst texture, 2=PorterDuff framebuffer fetch
//   HAS_DEVICE_MASK: device-space mask texture binding is present
#version 450

#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
// coverage_uniforms.inc declares the runtime clip contract fields (Rect, HasClip, rrect).
#define HAS_RUNTIME_CLIP 1
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_COMMON_COLOR
  vec4 Color;
#endif
  int StrokeEnabled;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif

#if HAS_DEVICE_MASK
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1
#else
#define XP_DST_TEX_BINDING 0
#endif

#if !HAS_COMMON_COLOR
#define FILL_COLOR FILL_COLOR_VARYING
#endif
#define FILL_COVERAGE FILL_COVERAGE_ELLIPSE
#if HAS_DEVICE_MASK
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_ALWAYS
#endif
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
