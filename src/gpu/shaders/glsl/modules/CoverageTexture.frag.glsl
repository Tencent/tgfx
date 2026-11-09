// CoverageTexture fragment hook: the subset clamp, the sample and the alpha-only broadcast,
// verbatim from the former kernels/shape_instanced_texture_coverage.frag. The sample replaces the
// running coverage — the instanced batch's per-vertex coverage slot is a constant stride filler,
// so the texture is the whole coverage of the draw.
void CoverageTexture_fragment(inout Fragment f) {
  highp vec2 finalCoord =
      clamp(ShapeInstancedGeometry_vCoord, CoverageTexture_Subset.xy, CoverageTexture_Subset.zw);
  vec4 coverage = texture(CoverageTexture_Sampler, finalCoord);
  if (CoverageTexture_AlphaOnly != 0) {
    coverage = vec4(coverage.r);
  }
  f.coverage = coverage;
}
