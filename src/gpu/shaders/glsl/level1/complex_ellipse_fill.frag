// ComplexEllipseFillShader fragment entry: ComplexEllipseGeometryProcessor +
// EmptyXferProcessor/PorterDuffXP. Offset-based region dispatch: corner (ellipse SDF), edge
// (linear), center (full) — complex_ellipse_coverage.inc, shared with the pointwise chain's
// GP_LAYOUT 3/4. The device-space mask is a runtime uniform (HasDeviceMask) with a padded dummy
// sampler, not a permutation dimension: ComplexEllipse draws are rare and every extra dimension
// doubles the whole family (stroke x XP). The pipeline is the shared dedicated one
// (fill_fragment_main.inc); this file declares the family's interface and selects the steps.
// The color attribute is unconditional: the exact Color uniform serves common-color draws
// through the HasCommonColor runtime uniform, the vColor varying per-vertex-colored ones.
// Fragment dimensions: STROKE, HAS_XP (int, 3).
#version 450

#ifndef STROKE
#define STROKE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#define HAS_RUNTIME_CLIP 1
#define HAS_RUNTIME_DEVICE_MASK 1

layout(std140, set = TGFX_FRAGMENT_UBO_SET, binding = TGFX_FRAGMENT_UBO_BINDING) uniform FragmentUniformBlock {
  // Exact paint color for common-color draws; the broadcast into the per-vertex attribute is
  // 8-bit quantized, so HasCommonColor selects this uniform when set.
  vec4 Color;
  int HasCommonColor;
#include "coverage_uniforms.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
layout(location = 2) in vec2 vEdgeDist;
layout(location = 3) in vec4 vColor;
#if STROKE
layout(location = 4) in vec2 vStrokeWidth;
#endif

// The mask sampler is always declared so the runtime branch needs no second variant; when the draw
// carries no mask FP the binding is padded with the shared dummy texture (ProgramInfo).
layout(set = TGFX_TEXTURE_SET, binding = 0) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING 1

#define FILL_COLOR FILL_COLOR_VARYING_OR_COMMON
#define FILL_COVERAGE FILL_COVERAGE_COMPLEX_ELLIPSE
#define FILL_DEVICE_MASK FILL_DEVICE_MASK_RUNTIME
#define FILL_CLIP 1
#include "fill_fragment_main.inc"
