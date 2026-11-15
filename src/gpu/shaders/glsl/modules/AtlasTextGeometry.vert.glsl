// AtlasTextGeometry vertex hook: the atlas coordinate scaled by the atlas size and the per-vertex
// coverage/color forwarded. The expressions are bit-exact by contract.
void AtlasTextGeometry_vertex(inout Vertex v) {
  v.position = AtlasTextGeometry_Position;
  AtlasTextGeometry_vTexCoords = AtlasTextGeometry_MaskCoord * AtlasTextGeometry_AtlasSizeInv;
#if AtlasTextGeometry_HAS_COVERAGE
  AtlasTextGeometry_vCoverage = AtlasTextGeometry_Coverage;
#endif
  AtlasTextGeometry_vColor = AtlasTextGeometry_InColor;
}
