// RoundStrokeRectGeometry vertex hook: the ellipse data and the per-vertex coverage forwarded to
// the fragment stage. The position is already in device space. The expressions are verbatim from
// the reference emission.
#include "pipeline/chain/pointwise_declared_config.inc"

void RoundStrokeRectGeometry_vertex(inout Vertex v) {
  v.position = RoundStrokeRectGeometry_Position;
  v.localCoord = v.position;
  RoundStrokeRectGeometry_vOffsets = RoundStrokeRectGeometry_InEllipseOffset;
  RoundStrokeRectGeometry_vCoverage = RoundStrokeRectGeometry_InCoverage;
  RoundStrokeRectGeometry_vRadii = RoundStrokeRectGeometry_InEllipseRadii;
  RoundStrokeRectGeometry_vColor = RoundStrokeRectGeometry_InColor;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
