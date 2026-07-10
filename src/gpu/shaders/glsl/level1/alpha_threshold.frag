// AlphaThresholdShader fragment shader
// No permutation dimensions — single variant.
// Applies step(threshold, alpha) to discard pixels below threshold.
#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  float Threshold;
};

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  float alpha = inputColor.a;
  float mask = step(Threshold, alpha);
  fragColor = inputColor * mask;
}
