// ComplexEllipseGeometry vertex hook: the ellipse data forwarded to the fragment stage. The stroke
// width attribute is unconditional and the StrokeEnabled uniform selects the coverage form, so one
// variant serves the fill and the stroke form. The expressions are bit-exact by contract.
#include "pipeline/chain/pointwise_declared_config.inc"

void ComplexEllipseGeometry_vertex(inout Vertex v) {
  v.position = ComplexEllipseGeometry_Position;
  v.localCoord = v.position;
  ComplexEllipseGeometry_vOffsets = ComplexEllipseGeometry_InEllipseOffset;
  ComplexEllipseGeometry_vRadii = ComplexEllipseGeometry_InEllipseRadii;
  ComplexEllipseGeometry_vEdgeDist = ComplexEllipseGeometry_InEdgeDist;
  ComplexEllipseGeometry_vColor = ComplexEllipseGeometry_InColor;
  ComplexEllipseGeometry_vStrokeWidth = ComplexEllipseGeometry_InStrokeWidth;

// The leaf and procedural-source coordinate transforms, the chain body's contract: the macros name
// this geometry's own coordinate source, the shared include spells the transforms once.
#define CHAIN_LEAF_SOURCE(k) v.localCoord
#define CHAIN_SOURCE_COORDS v.localCoord
#include "pipeline/chain/pointwise_vertex_coords.inc"
#undef CHAIN_LEAF_SOURCE
#undef CHAIN_SOURCE_COORDS
}
