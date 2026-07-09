// AlphaThresholdShader fragment shader
// No permutation dimensions — single variant.
// Applies step(threshold, alpha) to discard pixels below threshold.
#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
  float Threshold_P1;
};

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color_P0;
  float alpha = inputColor.a;
  float mask = step(Threshold_P1, alpha);
  fragColor = inputColor * mask;
}
