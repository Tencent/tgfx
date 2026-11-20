// HairlineQuadGeometry vertex hook: the quad edge data forwarded verbatim. The expressions are
// bit-exact by contract.
#include "pipeline/chain/pointwise_declared_config.inc"

void HairlineQuadGeometry_vertex(inout Vertex v) {
  v.position = (HairlineQuadGeometry_Matrix * vec3(HairlineQuadGeometry_Position, 1.0)).xy;
  HairlineQuadGeometry_vHairQuadEdge = HairlineQuadGeometry_InHairQuadEdge;
  HairlineQuadGeometry_vColor = vec4(0.0);

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
