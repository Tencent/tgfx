// MaskFillShader fragment shader
// Processor layout: DefaultGeometryProcessor + one alpha-only TextureEffect coverage FP +
// EmptyXferProcessor. The fill color is the DefaultGP Color uniform; the coverage is the alpha-only
// mask sampled at the per-vertex transformed coordinate.
//
// Only the empty (plain src-over) transfer processor is supported. For that case the coverage is a
// straight multiply into the premultiplied color, which matches the runtime exactly. Porter-Duff /
// framebuffer-fetch blends are NOT handled here (the matcher rejects them so they fall back to
// ProgramBuilder), because the coverage must feed the blend's coverage lerp and the framebuffer
// read differs from a simple sampler.
//
// The coverage sampler MUST be named TextureSampler_0: the precompiled path binds a fragment
// processor's texture by its (suffix-stripped) runtime name, which for the first TextureEffect is
// "TextureSampler_0".
#version 450

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

layout(location = 0) out vec4 fragColor;

void main() {
  float maskCoverage = texture(TextureSampler_0, TransformedCoords_0).r;
  fragColor = Color * maskCoverage;
}
