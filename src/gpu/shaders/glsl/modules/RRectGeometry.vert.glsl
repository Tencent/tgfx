// RRectGeometry vertex hook: forwards the geometry attributes into the varying interface. The
// stroke width attribute is unconditional and the StrokeEnabled uniform selects the coverage form,
// so one variant serves the fill and the stroke form. The expressions are bit-exact by contract.

#include "pipeline/chain/pointwise_declared_config.inc"

void RRectGeometry_vertex(inout Vertex v) {
  v.position = RRectGeometry_Position;
  v.localCoord = RRectGeometry_LocalCoord;
  RRectGeometry_vLocalCoord = v.localCoord;
  RRectGeometry_vRadii = RRectGeometry_Radii;
  RRectGeometry_vRectBounds = RRectGeometry_RectBounds;
  RRectGeometry_vColor = RRectGeometry_Color;
  RRectGeometry_vStrokeWidth = RRectGeometry_StrokeWidth;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
