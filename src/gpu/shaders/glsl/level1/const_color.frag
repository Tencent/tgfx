// ConstColorShader fragment shader
// Permutation dimensions (frag): INPUT_MODE (0=Ignore, 1=ModulateRGBA, 2=ModulateA)
#version 450

layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color_P0;
  vec4 Color_P1;
};

void main() {
  vec4 color = Color_P1;

#if INPUT_MODE == 1
  // ModulateRGBA: multiply by input color from previous stage
  color *= Color_P0;
#elif INPUT_MODE == 2
  // ModulateA: multiply by input alpha only
  color *= Color_P0.a;
#endif

  fragColor = color;
}
