// TextureClipShader fragment shader
// Processor layout: DefaultGeometryProcessor() + TextureEffect(,color) + AARectEffect(,coverage) + EmptyXferProcessor()
// Permutation dimensions (injected by build tool as #define 0/1):
//   ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET
#version 450

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
  vec4 Color;
#if HAS_SUBSET
  vec4 Subset;
#endif
#if HAS_RGBAAA
  vec2 AlphaStart;
#endif
  vec4 Rect;
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 outputColor = Color;
  highp vec2 texCoord = TransformedCoords_0;
  highp vec2 finalCoord = texCoord;

#if HAS_SUBSET
  finalCoord = clamp(finalCoord, Subset.xy, Subset.zw);
#endif

  vec4 texColor = texture(TextureSampler_0, finalCoord);

#if ALPHA_ONLY
  // Alpha-only textures use R8 format in Metal. Use .r to get the actual alpha value.
  texColor = vec4(texColor.r);
#endif

#if HAS_RGBAAA
  texColor = clamp(texColor, 0.0, 1.0);
  highp vec2 alphaCoord = finalCoord + AlphaStart;
  vec4 alpha = texture(TextureSampler_0, alphaCoord);
  alpha = clamp(alpha, 0.0, 1.0);
  texColor = vec4(texColor.rgb * alpha.r, alpha.r);
#endif

  // TextureEffect post-processing
#if ALPHA_ONLY
  texColor = texColor.a * outputColor;
#else
  texColor = texColor * outputColor.a;
#endif

  // AARectEffect: compute coverage from fragment position relative to clip rect
  // Coverage FP chain starts with outputCoverage = vec4(1.0) from DefaultGP (no coverage attr)
  highp vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
  highp vec2 dists2 = dists4.xy + dists4.zw - 1.0;
  highp float rectCoverage = dists2.x * dists2.y;
  vec4 outputCoverage = vec4(1.0) * rectCoverage;

  // EmptyXferProcessor: final = color * coverage
  fragColor = texColor * outputCoverage;
}
