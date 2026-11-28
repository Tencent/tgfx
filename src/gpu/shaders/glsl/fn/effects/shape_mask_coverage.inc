// Converts a signed distance to coverage over a one pixel wide linear ramp.
//   sdf     the signed distance in content pixels, positive outside the shape
//   return  the coverage at that distance, in [0, 1]
//@function shapeMaskCoverage
//@op ShapeMaskCoverage
//@frag
highp float shapeMaskCoverage(highp float sdf /*@in*/) {
  return clamp(0.5 - sdf, 0.0, 1.0);
}
