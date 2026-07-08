// Teaching/validation version with 4 dimensions; see §6.4 for the 8-dimension production
// version (P3-5).
#version 450

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;

layout(location = 0) out vec2 v_texCoord;

void main() {
  v_texCoord = a_texCoord;
  gl_Position = vec4(a_position, 0.0, 1.0);
}
