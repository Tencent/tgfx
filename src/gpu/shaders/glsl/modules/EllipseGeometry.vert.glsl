// EllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The expressions
// are bit-exact by contract.
#include "pipeline/chain/pointwise_declared_config.inc"

void EllipseGeometry_vertex(inout Vertex v) {
  v.position = EllipseGeometry_Position;
  // The raw position doubles as the local coordinate for any body module that maps it (the glass
  // family's background-coordinate transform); the Ellipse family's own body reads none.
  v.localCoord = EllipseGeometry_Position;
  EllipseGeometry_vOffsets = EllipseGeometry_InEllipseOffset;
  EllipseGeometry_vRadii = EllipseGeometry_InEllipseRadii;
  EllipseGeometry_vColor = EllipseGeometry_InColor;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
