// The chain's rect layout with the uvCoord attribute: the vertex hook mirrors the legacy text's
// uvCoord branch (the subset corners go through CoordTransformMatrix_0, as the quad GP wrote them)
// and flags CHAIN_UV_COORD_SOURCE so the body's transforms select the leaf source per leaf through
// CoordSourceMask.

#define GP_LAYOUT 0
#define HAS_COVERAGE 1
#define HAS_UV_COORD 1
#define HAS_SUBSET SUBSET
#define CHAIN_UV_COORD_SOURCE 1

void ChainRectUVGeometry_vertex(inout Vertex v) {
  highp vec2 position = (Matrix * vec3(aPosition, 1.0)).xy;
  v.position = position;
  v.localCoord = aPosition;
  vCoverage = inCoverage;
  vColor = inColor;
#if HAS_SUBSET
  highp vec2 subsetLT = (CoordTransformMatrix_0 * vec3(texSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (CoordTransformMatrix_0 * vec3(texSubset.zw, 1.0)).xy;
  vTexSubset = vec4(min(subsetLT, subsetRB), max(subsetLT, subsetRB));
#endif
}
