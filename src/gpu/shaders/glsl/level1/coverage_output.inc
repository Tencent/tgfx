// Coverage computation — include before xp_output.inc.
// Before including, #define TGFX_COVERAGE_SRC_COLOR to the uncovered color expression.
// Optionally #define TGFX_INITIAL_COVERAGE to the coverage this draw already carries (e.g. a
// geometry-AA vCoverage); AARect and mask coverage are then multiplied on top of it. Defaults to
// vec4(1.0), so shaders with no prior coverage are unaffected.
// After this file, TGFX_XP_SRC_COLOR is defined for xp_output.inc (coverage-premultiplied color for
// the non-XP path). When coverage is present, TGFX_XP_SRC_UNPREMUL (the uncovered color) and
// TGFX_XP_COVERAGE (the computed coverage) are also defined so the XferProcessor path can composite
// coverage correctly. TGFX_XP_COVERAGE is guarded so a shader that already declared its own coverage
// (e.g. a vertex vCoverage) keeps it instead of being overridden here.
#ifndef TGFX_INITIAL_COVERAGE
#define TGFX_INITIAL_COVERAGE vec4(1.0)
#endif
#ifndef HAS_RUNTIME_CLIP
#define HAS_RUNTIME_CLIP 0
#endif
#ifndef HAS_DEVICE_MASK
#define HAS_DEVICE_MASK 0
#endif
#ifndef HAS_RUNTIME_DEVICE_MASK
#define HAS_RUNTIME_DEVICE_MASK 0
#endif

#if HAS_RUNTIME_CLIP
  vec4 _coverageResult = TGFX_INITIAL_COVERAGE;

  #if HAS_RUNTIME_DEVICE_MASK
  if (HasDeviceMask != 0) {
    highp vec3 _maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    float _maskAlpha = texture(MaskTextureSampler, _maskCoord.xy).r;
    _coverageResult *= _maskAlpha;
  }
  #elif HAS_DEVICE_MASK
    highp vec3 _maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    float _maskAlpha = texture(MaskTextureSampler, _maskCoord.xy).r;
    _coverageResult *= _maskAlpha;
  #endif

  if (HasClip != 0) {
    highp vec4 _dists4 =
        clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
    highp vec2 _dists2 = _dists4.xy + _dists4.zw - 1.0;
    _coverageResult *= _dists2.x * _dists2.y;
  }

  vec4 _uncoveredColor = TGFX_COVERAGE_SRC_COLOR;
  vec4 _coveredColor = _uncoveredColor * _coverageResult;
#define TGFX_XP_SRC_COLOR _coveredColor
#define TGFX_XP_SRC_UNPREMUL _uncoveredColor
#ifndef TGFX_XP_COVERAGE
#define TGFX_XP_COVERAGE _coverageResult
#endif
#elif HAS_COVERAGE
  vec4 _coverageResult = TGFX_INITIAL_COVERAGE;

  #if HAS_COVERAGE == 2
    highp vec3 _maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    float _maskAlpha = texture(MaskTextureSampler, _maskCoord.xy).r;
    _coverageResult *= _maskAlpha;
  #endif

  highp vec4 _dists4 =
      clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
  highp vec2 _dists2 = _dists4.xy + _dists4.zw - 1.0;
  _coverageResult *= _dists2.x * _dists2.y;

  vec4 _uncoveredColor = TGFX_COVERAGE_SRC_COLOR;
  vec4 _coveredColor = _uncoveredColor * _coverageResult;
#define TGFX_XP_SRC_COLOR _coveredColor
#define TGFX_XP_SRC_UNPREMUL _uncoveredColor
#ifndef TGFX_XP_COVERAGE
#define TGFX_XP_COVERAGE _coverageResult
#endif
#else
#define TGFX_XP_SRC_COLOR TGFX_COVERAGE_SRC_COLOR
#endif
