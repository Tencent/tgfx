// Teaching/validation version with 4 dimensions; see §6.4 for the 8-dimension production
// version (P3-5).
#version 450

layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;

#if HAS_YUV
layout(set = 0, binding = 0) uniform sampler2D u_texY;
layout(set = 0, binding = 1) uniform sampler2D u_texU;
layout(set = 0, binding = 2) uniform sampler2D u_texV;
#else
layout(set = 0, binding = 0) uniform sampler2D u_sampler;
#if HAS_RGBAAA
layout(set = 0, binding = 1) uniform sampler2D u_alphaSampler;
#endif
#endif

#if HAS_SUBSET
layout(set = 0, binding = 3) uniform SubsetBlock { vec4 u_subset; };
#endif

void main() {
  vec2 coord = v_texCoord;

#if HAS_SUBSET
  coord = clamp(coord, u_subset.xy, u_subset.zw);
#endif

#if HAS_YUV
  float y = texture(u_texY, coord).r;
  float u = texture(u_texU, coord).r - 0.5;
  float v = texture(u_texV, coord).r - 0.5;
  vec4 color = vec4(y + 1.402 * v, y - 0.344 * u - 0.714 * v, y + 1.772 * u, 1.0);
#else
  vec4 color = texture(u_sampler, coord);
#if ALPHA_ONLY
  color = vec4(0.0, 0.0, 0.0, color.a);
#elif HAS_RGBAAA
  color.a = texture(u_alphaSampler, coord).r;
#endif
#endif

  fragColor = color;
}
