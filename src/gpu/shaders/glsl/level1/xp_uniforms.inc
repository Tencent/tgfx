// XP (XferProcessor) uniform declarations — include inside FragmentUniformBlock.
// Guarded by HAS_XP; expands to nothing when HAS_XP == 0.
#if HAS_XP
  vec2 DstTextureUpperLeft;
  vec2 DstTextureCoordScale;
  int XPBlendMode;
#endif
