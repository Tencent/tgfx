// DeviceSpaceTextureShader fragment shader
// Permutation dimensions (frag): ALPHA_ONLY
// Samples texture using device-space (screen) coordinates via gl_FragCoord.
#version 450

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D TextureSampler_0;

layout(std140, set = 0, binding = 2) uniform FragmentUniformBlock {
  vec4 Color;
  mat3 DeviceCoordMatrix;
};

void main() {
  highp vec3 deviceCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  vec4 color = texture(TextureSampler_0, deviceCoord.xy);

#if ALPHA_ONLY
  color = color.a * Color;
#else
  color = color * Color.a;
#endif

  fragColor = color;
}
