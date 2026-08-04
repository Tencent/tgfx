// QuadColorFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor/PorterDuffXP (no FP)
// Permutation dimensions: HAS_COVERAGE, HAS_XP
//
// Color comes only from the vColor varying: rect vertex providers always write a color slot,
// broadcasting the record's paint color for uniform-color batches, so the Color uniform and the
// HAS_COLOR dimension no longer exist here.
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif

#if HAS_XP || HAS_MASK_TEXTURE
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
#endif
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
};
#endif

#if HAS_COVERAGE
layout(location = 0) in float vCoverage;
#endif

layout(location = 1) in vec4 vColor;

// The mask texture (device-space coverage) occupies binding 0; the XP dst texture, if any, follows.
#if HAS_MASK_TEXTURE
layout(set = 1, binding = 0) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING 1
#else
  #define XP_DST_TEX_BINDING 0
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = vColor;

#if HAS_MASK_TEXTURE
  // Device-space mask coverage: multiply the coverage sampled from the mask texture into the color.
  // Geometry AA (vCoverage) is applied separately below, so the two coverage sources compose.
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  float maskAlpha = texture(MaskTextureSampler, maskCoord.xy).r;
  outputColor *= maskAlpha;
#endif

#if HAS_XP
  #if HAS_COVERAGE
  fragColor = applyPorterDuffXP(outputColor, vec4(vCoverage));
  #else
  fragColor = applyPorterDuffXP(outputColor, vec4(1.0));
  #endif
#else
  // EmptyXferProcessor: output = outputColor * outputCoverage
  #if HAS_COVERAGE
  fragColor = outputColor * vCoverage;
  #else
  fragColor = outputColor;
  #endif
#endif
}
