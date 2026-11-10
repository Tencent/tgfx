// The chain body's vertex hook: the leaf and procedural-source coordinate transforms, reading the
// coordinate source the geometry hook stored in Vertex::localCoord. The CHAIN_LEAF_SOURCE macros
// are the same contract the legacy kernel defines from its layout's attributes — the uvCoord
// layouts keep the per-leaf runtime selection through CoordSourceMask.

#include "pipeline/chain/pointwise_declared_config.inc"

#ifdef CHAIN_UV_COORD_SOURCE
#define CHAIN_LEAF_SOURCE(k) ((((CoordSourceMask >> (k)) & 1) != 0) ? uvCoord : v.localCoord)
#define CHAIN_SOURCE_COORDS ((((CoordSourceMask >> 4) & 1) != 0) ? uvCoord : v.localCoord)
#else
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#endif

void Chain_vertex(inout Vertex v) {
#include "pipeline/chain/pointwise_vertex_coords.inc"
}
