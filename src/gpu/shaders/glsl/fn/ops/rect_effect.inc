// Rect effect coverage: the separable 1D box-filter product gives the AA band 0.5 coverage on
// the edge, 1.0 inside and 0.0 outside; the non-AA arm is a hard step on the same edges. The
// device-to-local map is always supplied (identity when no transform), so both arms share one
// signature and the driver folds the identity multiply.
//@op RectCoverage
//@frag
vec4 pmRectEffect(vec4 color /*@in*/, vec2 fragCoord /*@in*/, vec4 rect, float antiAlias,
                  mat3 deviceToLocal) {
  highp vec3 hl = deviceToLocal * vec3(fragCoord, 1.0);
  highp vec2 local = hl.xy / hl.z;
  highp vec4 dAA = clamp(vec4(local - rect.xy, rect.zw - local), -0.5, 0.5);
  highp vec2 aaCovXY = dAA.xy + dAA.zw;
  highp float aaCov = aaCovXY.x * aaCovXY.y;
  highp float nonAACov = step(rect.x, local.x) * step(local.x, rect.z) * step(rect.y, local.y) *
                         step(local.y, rect.w);
  highp float coverage = mix(nonAACov, aaCov, antiAlias);
  return color * coverage;
}
