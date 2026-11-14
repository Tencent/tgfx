// The rect (uvless) layout's interface facts for the fragment stage. The geometry modules own no
// fragment work (the chain body computes coverage and color itself), but the body's shared text
// branches on the legacy macro names, so the module declares them here — before the body's include,
// which the assembler emits after this slot.

#define HAS_COVERAGE 1
#define HAS_UV_COORD 0
#define HAS_SUBSET SUBSET

void ChainRectGeometry_fragment(inout Fragment f) {
}
