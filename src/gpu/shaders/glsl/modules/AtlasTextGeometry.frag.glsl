// AtlasTextGeometry fragment hook: the atlas sample — an alpha-only atlas contributes its red
// channel as the coverage and leaves the color, a color atlas replaces the color with its
// unpremultiplied rgb and contributes its alpha. The expressions are verbatim from the former
// fill pipeline's FILL_COLOR_VARYING_OR_COMMON and FILL_COVERAGE_ATLAS_TEXT.
void AtlasTextGeometry_fragment(inout Fragment f) {
  f.color = AtlasTextGeometry_vColor;
  if (AtlasTextGeometry_HasCommonColor != 0) {
    f.color = AtlasTextGeometry_Color;
  }
  vec4 atlasTexColor = texture(AtlasTextGeometry_Sampler, AtlasTextGeometry_vTexCoords);
#if AtlasTextGeometry_HAS_COVERAGE
  float totalCoverage = AtlasTextGeometry_vCoverage;
#else
  float totalCoverage = 1.0;
#endif
  if (AtlasTextGeometry_AlphaOnly != 0) {
    totalCoverage = atlasTexColor.r;
  } else {
    f.color = clamp(vec4(atlasTexColor.rgb / atlasTexColor.a, 1.0), 0.0, 1.0);
    totalCoverage = atlasTexColor.a;
  }
  f.coverage = vec4(totalCoverage);
}
