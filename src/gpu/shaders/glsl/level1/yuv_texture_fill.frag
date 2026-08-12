// YUVTextureFillShader fragment shader
// Processor layout: QuadPerEdgeAAGeometryProcessor + TextureEffect(YUV) +
// EmptyXferProcessor/PorterDuffXP
// Permutation dimensions (frag): YUV_FORMAT (0=I420 three planes, 1=NV12 two planes), HAS_XP.
// The plane sampling and conversion mirror GLSLTextureEffect::emitYUVTextureCode; the limited
// range offset is a runtime uniform (YUVLimitedRange) since it is pure fragment math.
#version 450

#ifndef YUV_FORMAT
#define YUV_FORMAT 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Rect;
  int HasClip;
  // Always present; holds the full texture bounds when the source has no real subset, so the
  // clamp degenerates to a no-op.
  vec4 Subset;
  mat3 Mat3ColorConversion;
  int YUVLimitedRange;
  // RGBAAA dual-plane alpha: always declared, only read when HasRgbaaa != 0 (runtime uniform
  // branch, mirroring QuadTextureFillShader).
  vec2 AlphaStart;
  int HasRgbaaa;
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
  int OutputAlphaSwizzle;
};

layout(location = 0) in vec3 TransformedCoords_0;
layout(location = 1) in float vCoverage;
layout(location = 2) in vec4 vColor;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
#if YUV_FORMAT == 0
layout(set = 1, binding = 2) uniform sampler2D TextureSampler_2;
  #define XP_DST_TEX_BINDING 3
#else
  #define XP_DST_TEX_BINDING 2
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"
#include "aa_rect_clip_coverage.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = vColor;

  highp vec2 texCoord = TransformedCoords_0.xy / TransformedCoords_0.z;
  highp vec2 finalCoord = clamp(texCoord, Subset.xy, Subset.zw);

  vec3 yuv;
  yuv.x = texture(TextureSampler_0, finalCoord).r;
#if YUV_FORMAT == 0
  yuv.y = texture(TextureSampler_1, finalCoord).r;
  yuv.z = texture(TextureSampler_2, finalCoord).r;
#else
  yuv.yz = texture(TextureSampler_1, finalCoord).ra;
#endif
  if (YUVLimitedRange != 0) {
    yuv.x -= (16.0 / 255.0);
  }
  yuv.yz -= vec2(0.5, 0.5);
  vec3 rgb = clamp(Mat3ColorConversion * yuv, 0.0, 1.0);
  vec4 color;
  if (HasRgbaaa != 0) {
    // The alpha plane lives in a separate region of the Y plane, reached by AlphaStart. The
    // remap arithmetic is verbatim from the runtime codegen.
    highp vec2 alphaVertexColor = finalCoord + AlphaStart;
    float yuv_a = texture(TextureSampler_0, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    color = vec4(rgb * yuv_a, yuv_a);
  } else {
    color = vec4(rgb, 1.0);
  }
  color = color * outputColor.a;

  float totalCoverage = vCoverage * aaRectClipCoverage();

#if HAS_XP
  fragColor = applyPorterDuffXP(color, vec4(totalCoverage));
#else
  fragColor = color * totalCoverage;
#endif
#include "output_swizzle.inc"
}
