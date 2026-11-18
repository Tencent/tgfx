// The composed epilogue for a rect chain without the xfer's advanced blend pass and without a
// device mask (leaf-free profiles carry none): the coverage left-fold — the geometry's own
// coverage, then the clip product — and the output swizzle. The two placeholders are structural
// facts the assembler substitutes: the clip coverage expression and the final coverage
// expression before the clip folds in.
  vec4 clipCoverage = __TGFX_CLIP_COVERAGE__;
  highp vec4 finalCoverage = __TGFX_FINAL_COVERAGE__;
  finalCoverage *= clipCoverage;
  fragColor = (result * finalCoverage);
  if (OutputAlphaSwizzle != 0) {
    fragColor = fragColor.aaaa;
  }
