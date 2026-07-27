// MaskFillShader fragment shader
// Processor layout: DefaultGeometryProcessor + one alpha-only TextureEffect coverage FP + XP.
// The fill color is the DefaultGP Color uniform; the coverage is the alpha-only mask sampled at the
// per-vertex transformed coordinate.
//
// Permutation dimensions (frag): HAS_XP (int, 3) — 0=none(plain multiply), 1=PorterDuff DST_TEX,
// 2=PorterDuff framebuffer-fetch. The mask coverage feeds the blend's coverage lerp, so advanced /
// coefficient blends composite correctly instead of pre-multiplying coverage into the source.
//
// The coverage sampler MUST be named TextureSampler_0: the precompiled path binds a fragment
// processor's texture by its (suffix-stripped) runtime name, which for the first TextureEffect is
// "TextureSampler_0".
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;

#define XP_DST_TEX_BINDING 1
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

void main() {
  float maskCoverage = texture(TextureSampler_0, TransformedCoords_0).r;
// Coverage comes entirely from the mask. Keep the un-premultiplied color for the XferProcessor lerp
// and fold coverage into the non-XP output (matches the coverage contract in xp_output.inc).
#define TGFX_XP_SRC_COLOR (Color * maskCoverage)
#define TGFX_XP_SRC_UNPREMUL Color
#define TGFX_XP_COVERAGE vec4(maskCoverage)
#include "xp_output.inc"
}
