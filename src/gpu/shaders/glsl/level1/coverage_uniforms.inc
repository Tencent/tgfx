// Coverage uniform declarations — include inside FragmentUniformBlock.
// Guarded by HAS_COVERAGE; expands to nothing when HAS_COVERAGE == 0.
#if HAS_COVERAGE
  vec4 Rect;
#endif
#if HAS_COVERAGE == 2
  mat3 DeviceCoordMatrix;
#endif
