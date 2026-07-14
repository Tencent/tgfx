// GaussianBlur1DShader fragment shader
// Permutation dimensions (frag): MAX_SIGMA (0~9, maps to maxSigma 1~10), HAS_XP
// Loop upper bound = 4 * (MAX_SIGMA + 1), covering the full Gaussian kernel radius.
#version 450

#ifndef MAX_SIGMA
#define MAX_SIGMA 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Sigma;
  vec2 Step;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  float sigma = Sigma;
  vec2 offset = Step;
  int radius = int(ceil(2.0 * sigma));

  vec4 sum = vec4(0.0);
  float total = 0.0;

  for (int j = 0; j <= 4 * (MAX_SIGMA + 1); ++j) {
    int i = j - radius;
    float weight = exp(-float(i * i) / (2.0 * sigma * sigma));
    total += weight;

    vec2 sampleCoord = TransformedCoords_0 + offset * float(i);
    vec4 texColor = texture(TextureSampler_0, sampleCoord);
    sum += texColor * weight;

    if (i == radius) {
      break;
    }
  }

  vec4 blurResult = sum / total;
#define TGFX_XP_SRC_COLOR blurResult
#include "xp_output.inc"
}
