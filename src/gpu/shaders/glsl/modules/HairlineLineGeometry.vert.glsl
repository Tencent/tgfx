// HairlineLineGeometry vertex hook: the edge distance forwarded verbatim. The expressions are
// bit-exact by contract.
#include "pipeline/chain/pointwise_declared_config.inc"

void HairlineLineGeometry_vertex(inout Vertex v) {
  v.position = (HairlineLineGeometry_Matrix * vec3(HairlineLineGeometry_Position, 1.0)).xy;
  v.localCoord = v.position;
  HairlineLineGeometry_vEdge = HairlineLineGeometry_InEdge;
  HairlineLineGeometry_vColor = HairlineLineGeometry_InColor;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
