// XP (XferProcessor) output logic — include at the final output point in main().
// Before including, #define TGFX_XP_SRC_COLOR to the source color expression (already premultiplied
// by any local coverage; used directly on the non-XP output path).
// Optionally #define TGFX_XP_COVERAGE to the coverage expression (defaults to vec4(1.0)).
// Optionally #define TGFX_XP_SRC_UNPREMUL to the source color WITHOUT local coverage
// premultiplication. The XferProcessor path uses this together with TGFX_XP_COVERAGE so advanced
// blend modes composite coverage correctly as coverage*blend + (1-coverage)*dst, instead of
// blending a coverage-premultiplied source (which is only equivalent for SrcOver-style modes).
// Defaults to TGFX_XP_SRC_COLOR so shaders that do not set it keep their previous behavior exactly.
#ifndef TGFX_XP_COVERAGE
#define TGFX_XP_COVERAGE vec4(1.0)
#endif
#ifndef TGFX_XP_SRC_UNPREMUL
#define TGFX_XP_SRC_UNPREMUL TGFX_XP_SRC_COLOR
#endif

#if HAS_XP
  fragColor = applyPorterDuffXP(TGFX_XP_SRC_UNPREMUL, TGFX_XP_COVERAGE);
#else
  fragColor = TGFX_XP_SRC_COLOR;
#endif

#undef TGFX_XP_SRC_COLOR
#undef TGFX_XP_COVERAGE
#undef TGFX_XP_SRC_UNPREMUL
