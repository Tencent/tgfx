// PointwiseChainShader vertex stage. HAS_SOURCE_COORDS (mirrored from the RESOURCE_KIND tail)
// selects the extended form: the resource kinds carry the SourceCoords varying and reserve
// CoordTransformMatrix_0 for the source module (see pointwise_vertex.inc).
#version 450

#ifndef HAS_SOURCE_COORDS
#define HAS_SOURCE_COORDS 0
#endif
#if HAS_SOURCE_COORDS
#define TGFX_POINTWISE_SOURCE_COORDS 1
#endif

#include "pipeline/chain/pointwise_vertex.inc"
