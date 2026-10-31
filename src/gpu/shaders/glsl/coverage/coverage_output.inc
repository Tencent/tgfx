// Coverage fold for the opaque kernels — include at the output point in main().
// Requires: with HAS_RUNTIME_CLIP, clip_coverage.inc must already be included (file scope) so the
// clipCoverage() function is available for the HasClip branch.
// Before including, #define TGFX_COVERAGE_SRC_COLOR to the uncovered color expression.
// Optionally #define TGFX_INITIAL_COVERAGE to the coverage this draw already carries (e.g. a
// geometry-AA vCoverage); the device-mask and clip terms are then multiplied on top of it.
// Defaults to vec4(1.0), so shaders with no prior coverage are unaffected.
// The fold order — the initial coverage first, then the device mask, then the clip — is the
// shared left-fold contract (see compose.inc); this file ends at that shared composition site,
// so the caller must NOT include xp_output.inc itself.
#ifndef TGFX_INITIAL_COVERAGE
#define TGFX_INITIAL_COVERAGE vec4(1.0)
#endif
#ifndef HAS_RUNTIME_CLIP
#define HAS_RUNTIME_CLIP 0
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
  #endif

  if (HasClip != 0) {
    _coverageResult *= clipCoverage();
  }

  vec4 _uncoveredColor = TGFX_COVERAGE_SRC_COLOR;
#define TGFX_COMPOSE_SRC_UNPREMUL _uncoveredColor
#define TGFX_COMPOSE_COVERAGE _coverageResult
#include "coverage/compose.inc"
#else
#define TGFX_COMPOSE_SRC_UNPREMUL TGFX_COVERAGE_SRC_COLOR
#define TGFX_COMPOSE_COVERAGE vec4(1.0)
#include "coverage/compose.inc"
#endif
