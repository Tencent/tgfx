// TextureFillShader fragment shader
// Processor layout: DefaultGeometryProcessor(_P0) + TextureEffect(_P1) + EmptyXferProcessor(_P2)
// Permutation dimensions (injected by build tool as #define 0/1):
//   HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET
#version 450

#ifndef HAS_YUV
#define HAS_YUV 0
#endif
#ifndef ALPHA_ONLY
#define ALPHA_ONLY 0
#endif
#ifndef HAS_RGBAAA
#define HAS_RGBAAA 0
#endif
#ifndef HAS_SUBSET
#define HAS_SUBSET 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  // GP uniform — always first (registered by emitAndInstallGeoProc after RTAdjust)
  vec4 Color_P0;
  // TextureEffect uniforms — order matches emitCode() registration
#if HAS_SUBSET
  vec4 Subset_P1;
#endif
#if HAS_YUV
  mat3 Mat3ColorConversion_P1;
#endif
#if HAS_RGBAAA
  vec2 AlphaStart_P1;
#endif
};

layout(location = 0) in vec2 TransformedCoords_0;

// Samplers — mangled name: TextureSampler_{index}_P1
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0_P1;
#if HAS_YUV
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1_P1;
// I420 needs 3 planes; NV12 needs 2. Use 3 for maximum coverage; unused samplers are harmless.
layout(set = 1, binding = 2) uniform sampler2D TextureSampler_2_P1;
#endif

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor_P0;
  vec4 outputCoverage_P0;

  // --- GeometryProcessor output ---
  outputCoverage_P0 = vec4(1.0);
  outputColor_P0 = Color_P0;

  // --- TextureEffect ---
  vec4 output_P1;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset_P1.xy, Subset_P1.zw);
#endif

#if HAS_YUV == 0
  // Default (non-YUV) texture path
  vec4 color = texture(TextureSampler_0_P1, finalCoord);
#if HAS_RGBAAA
  color = clamp(color, 0.0, 1.0);
  highp vec2 alphaCoord = finalCoord + AlphaStart_P1;
  vec4 alpha = texture(TextureSampler_0_P1, alphaCoord);
  alpha = clamp(alpha, 0.0, 1.0);
  color = vec4(color.rgb * alpha.r, alpha.r);
#endif
  output_P1 = color;
#else
  // YUV texture path
  vec3 yuv;
  yuv.x = texture(TextureSampler_0_P1, finalCoord).r;
  // Assume I420 layout (3 planes). NV12 would need a separate variant dimension.
  highp vec2 uvCoord = finalCoord;
#if HAS_SUBSET
  uvCoord = clamp(texCoord, Subset_P1.xy, Subset_P1.zw);
#endif
  yuv.y = texture(TextureSampler_1_P1, uvCoord).r;
#if HAS_SUBSET
  uvCoord = clamp(texCoord, Subset_P1.xy, Subset_P1.zw);
#endif
  yuv.z = texture(TextureSampler_2_P1, uvCoord).r;

  // Limited range offset (always applied for safety; full range has offset of 0)
  yuv.x -= (16.0 / 255.0);
  yuv.yz -= vec2(0.5, 0.5);
  vec3 rgb = clamp(Mat3ColorConversion_P1 * yuv, 0.0, 1.0);

#if HAS_RGBAAA
  highp vec2 alphaCoord = finalCoord + AlphaStart_P1;
  float yuv_a = texture(TextureSampler_0_P1, alphaCoord).r;
  yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
  yuv_a = clamp(yuv_a, 0.0, 1.0);
  output_P1 = vec4(rgb * yuv_a, yuv_a);
#else
  output_P1 = vec4(rgb, 1.0);
#endif
#endif

  // Post-processing: alpha multiply
#if ALPHA_ONLY
  output_P1 = output_P1.a * outputColor_P0;
#else
  output_P1 = output_P1 * outputColor_P0.a;
#endif

  // EmptyXferProcessor — passthrough
  fragColor = output_P1 * outputCoverage_P0;
}
