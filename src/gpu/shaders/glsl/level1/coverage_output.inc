// Coverage computation — include before xp_output.inc.
// Before including, #define TGFX_COVERAGE_SRC_COLOR to the uncovered color expression.
// After this file, TGFX_XP_SRC_COLOR is defined for xp_output.inc.
#if HAS_COVERAGE
  vec4 _coveredColor;
  {
    vec4 _coverageResult = vec4(1.0);

#if HAS_COVERAGE == 2
    highp vec3 _maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    float _maskAlpha = texture(MaskTextureSampler, _maskCoord.xy).r;
    _coverageResult *= _maskAlpha;
#endif

    // AARectEffect
    highp vec4 _dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
    highp vec2 _dists2 = _dists4.xy + _dists4.zw - 1.0;
    highp float _rectCoverage = _dists2.x * _dists2.y;
    _coverageResult *= _rectCoverage;

    _coveredColor = TGFX_COVERAGE_SRC_COLOR * _coverageResult;
  }
#define TGFX_XP_SRC_COLOR _coveredColor
#else
#define TGFX_XP_SRC_COLOR TGFX_COVERAGE_SRC_COLOR
#endif
