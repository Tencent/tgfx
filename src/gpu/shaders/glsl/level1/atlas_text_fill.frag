// AtlasTextFillShader fragment shader
// Processor layout: AtlasTextGeometryProcessor() + EmptyXferProcessor()
// Permutation dimensions (injected as #define 0/1):
//   HAS_COVERAGE, HAS_COMMON_COLOR, ALPHA_ONLY
#version 450

#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COMMON_COLOR
#define HAS_COMMON_COLOR 0
#endif
#ifndef ALPHA_ONLY
#define ALPHA_ONLY 0
#endif

#if HAS_COMMON_COLOR
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
};
#endif

layout(location = 0) in vec2 vTextureCoords;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#if !HAS_COMMON_COLOR
layout(location = 2) in vec4 vColor;
#endif
#else
#if !HAS_COMMON_COLOR
layout(location = 1) in vec4 vColor;
#endif
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

void main() {
  // Determine output color from GP
#if HAS_COMMON_COLOR
  vec4 outputColor = Color;
#else
  vec4 outputColor = vColor;
#endif

  // Determine output coverage from GP
#if HAS_COVERAGE
  vec4 outputCoverage = vec4(vCoverage);
#else
  vec4 outputCoverage = vec4(1.0);
#endif

  // Sample atlas texture
  vec4 texColor = texture(TextureSampler_0, vTextureCoords);

#if ALPHA_ONLY
  // Alpha-only texture: coverage comes from texture alpha
  outputCoverage = vec4(texColor.a);
#else
  // Color texture (e.g. color emoji): extract color and coverage
  outputColor = clamp(vec4(texColor.rgb / texColor.a, 1.0), 0.0, 1.0);
  outputCoverage = vec4(texColor.a);
#endif

  // EmptyXferProcessor: final color = outputColor * outputCoverage
  fragColor = outputColor * outputCoverage;
}
