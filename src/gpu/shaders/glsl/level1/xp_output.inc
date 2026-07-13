// XP (XferProcessor) output logic — include at the final output point in main().
// Before including, #define TGFX_XP_SRC_COLOR to the source color expression.
// Optionally #define TGFX_XP_COVERAGE to the coverage expression (defaults to vec4(1.0)).
#ifndef TGFX_XP_COVERAGE
#define TGFX_XP_COVERAGE vec4(1.0)
#endif

#if HAS_XP
  fragColor = applyPorterDuffXP(TGFX_XP_SRC_COLOR, TGFX_XP_COVERAGE);
#else
  fragColor = TGFX_XP_SRC_COLOR;
#endif

#undef TGFX_XP_SRC_COLOR
#undef TGFX_XP_COVERAGE
