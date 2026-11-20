// The atlas layout's interface facts for the fragment stage, and the geometry contract: the hook
// hands the body its color and the constant coverage (the glyph mask reaches the composite through
// the clip-coverage register, not this input), so the body reads no layout-specific interface name.

#define HAS_COVERAGE 0
#define HAS_UV_COORD 1
#define HAS_SUBSET 0
#define CHAIN_UV_COORD_SOURCE 1
#define CHAIN_GEOMETRY_HOOK_CONTRACT 1
#define CHAIN_GEOMETRY_OWNS_COVERAGE 0

void ChainAtlasGeometry_fragment(inout Fragment f) {
  f.color = (ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : ChainAtlasGeometry_vColor;
  f.coverage = vec4(1.0);
}
