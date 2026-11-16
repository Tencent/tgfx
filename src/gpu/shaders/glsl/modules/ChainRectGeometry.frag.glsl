// The rect (uvless) layout's interface facts for the fragment stage, and the geometry contract:
// the hook hands the body its color and coverage, so the body reads no layout-specific interface
// name. The shared text still branches on the legacy contract macro names, so the module declares
// them here — before the body's include, which the assembler emits after this slot.

#define HAS_COVERAGE 1
#define HAS_UV_COORD 0
#define HAS_SUBSET SUBSET
#define CHAIN_GEOMETRY_HOOK_CONTRACT 1
#define CHAIN_GEOMETRY_OWNS_COVERAGE 1

void ChainRectGeometry_fragment(inout Fragment f) {
  f.color = (ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor;
  f.coverage = vec4(vCoverage);
}
