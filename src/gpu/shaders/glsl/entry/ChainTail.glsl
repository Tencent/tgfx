// The composed epilogue: the coverage left-fold — the coverage expression, then the clip product —
// and the output. Three placeholders are structural facts the assembler substitutes: the clip
// coverage expression, the final coverage expression before the clip folds in, and the output
// statement (the plain multiply, the XferProcessor's porter-duff composite, or its advanced blend
// pass composite).
  vec4 clipCoverage = __TGFX_CLIP_COVERAGE__;
  highp vec4 finalCoverage = __TGFX_FINAL_COVERAGE__;
  finalCoverage *= clipCoverage;
  __TGFX_OUTPUT__
  if (OutputAlphaSwizzle != 0) {
    fragColor = fragColor.aaaa;
  }
