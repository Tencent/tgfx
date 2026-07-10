// LumaShader fragment shader
// No permutation dimensions — single variant.
// Computes luminance from input color using configurable coefficients.
#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec3 LumaCoeffs;
};

layout(location = 0) out vec4 fragColor;

void main() {
  vec4 inputColor = Color;
  // Unpremultiply to get linear RGB
  vec3 rgb = (inputColor.a > 0.0) ? inputColor.rgb / inputColor.a : vec3(0.0);
  // Compute luminance using BT.709 or custom coefficients
  float luma = dot(rgb, LumaCoeffs);
  // Output premultiplied grayscale
  fragColor = vec4(luma * inputColor.a, luma * inputColor.a, luma * inputColor.a, inputColor.a);
}
