// The math and evaluation modules the chain's interface binds to: the geometry's own coverage
// formula for the layouts that carry one, the shared XferProcessor blend, and the texture sampling
// helpers the evaluation loop references in every variant (the dispatch is runtime-uniform, so
// texture_sampling.inc stays unconditional; only the tiled state needs leaves). The two halves
// live in their own files because the declared Chain assembly binds them at different points:
// the geometry coverage belongs to the fragment hook's prologue, the xp and sampling forms to the
// chain body's; the legacy kernel includes both here in its single include list.

#ifndef TGSL_POINTWISE_INTERFACE_SUPPORT_INCLUDED
#define TGSL_POINTWISE_INTERFACE_SUPPORT_INCLUDED

#include "pipeline/chain/pointwise_geometry_coverage.inc"
#include "pipeline/chain/pointwise_xp_support.inc"


#endif  // TGSL_POINTWISE_INTERFACE_SUPPORT_INCLUDED
