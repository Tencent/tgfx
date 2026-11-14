// The chain's rect layout (uvless) vertex hook. The legacy macro names are defined here so the
// shared body text compiles verbatim: GP_LAYOUT selects the body's per-layout branches, and
// HAS_SUBSET aliases the declared SUBSET dim. Vertex::localCoord carries the leaf coordinate
// source the body's transforms read.

#define GP_LAYOUT 0
#define HAS_COVERAGE 1
#define HAS_UV_COORD 0
#define HAS_SUBSET SUBSET

void ChainRectGeometry_vertex(inout Vertex v) {
  highp vec2 position = (Matrix * vec3(Position, 1.0)).xy;
  v.position = position;
  v.localCoord = Position;
  vCoverage = InCoverage;
  vColor = InColor;
#if HAS_SUBSET
  // The subset corners in leaf 0's texture space (the legacy vertex text verbatim; the uvless
  // form goes through its own transform, as the quad GP wrote it).
  highp vec2 subsetLT = (InTexSubsetMatrix * vec3(InTexSubset.xy, 1.0)).xy;
  highp vec2 subsetRB = (InTexSubsetMatrix * vec3(InTexSubset.zw, 1.0)).xy;
  vTexSubset = vec4(min(subsetLT, subsetRB), max(subsetLT, subsetRB));
#endif
}
