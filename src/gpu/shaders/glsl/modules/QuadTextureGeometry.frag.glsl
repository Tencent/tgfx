// QuadTextureGeometry fragment hook: the paint color the texture source multiplies by — the
// per-vertex color, or the exact Color uniform when HasCommonColor is set (the former
// FILL_COLOR_VARYING_OR_COMMON). The texture source reads it as its outputColor.
void QuadTextureGeometry_fragment(inout Fragment f) {
  f.color = QuadTextureGeometry_vColor;
  if (QuadTextureGeometry_HasCommonColor != 0) {
    f.color = QuadTextureGeometry_Color;
  }
  f.coverage = vec4(QuadTextureGeometry_vCoverage);
}
