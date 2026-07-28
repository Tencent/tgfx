// Applies the render-target write swizzle to the final fragColor. Only ALPHA_8 targets need a
// non-identity swizzle (Swizzle::ForWrite(ALPHA_8) == AAAA); every other format is RGBA (a no-op).
// OutputAlphaSwizzle is a runtime uniform set from ProgramInfo::getOutputSwizzle() — 1 when the
// render target is alpha-only — so a single precompiled variant serves both cases. Include at the
// end of main(), after fragColor has been assigned. Requires `int OutputAlphaSwizzle;` in the
// FragmentUniformBlock.
if (OutputAlphaSwizzle != 0) {
  fragColor = fragColor.aaaa;
}
