#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Subset;
  int AlphaOnly;
};

layout(location = 0) in vec2 TransformedCoords_0;
layout(location = 1) in vec4 vColor;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

void main() {
  highp vec2 finalCoord = clamp(TransformedCoords_0, Subset.xy, Subset.zw);
  vec4 coverage = texture(TextureSampler_0, finalCoord);
  if (AlphaOnly != 0) {
    coverage = vec4(coverage.r);
  }
  fragColor = vColor * coverage;
}
