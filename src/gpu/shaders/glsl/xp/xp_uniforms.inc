// XP (XferProcessor) uniform declarations — include inside FragmentUniformBlock.
// Guarded by HAS_XP; expands to nothing when HAS_XP == 0.
#if HAS_XP == 1
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#elif HAS_XP == 2
  int XPBlendMode;
#endif
