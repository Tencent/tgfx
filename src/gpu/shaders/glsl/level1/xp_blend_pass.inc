// The XferProcessor's advanced blend as one more pass of a kernel's evaluation loop.
//
// A kernel whose loop blends (the chain's OP_BLEND instruction, the pointwise OP_BLEND operator)
// and that also composites through a dst-reading XferProcessor would otherwise inline
// xpBlendColors twice: once in the loop, once in applyPorterDuffXP's advanced-mode fallback. GPU
// compilers inline every call site, so that is two copies of the largest function in the kernel.
// Instead such a kernel reads the dst color ahead of its loop, runs the loop one pass longer when
// the XferProcessor mode is an advanced one, feeds that pass the XferProcessor's source and dst so
// the loop's blend site computes xpBlendColors(src, dst, XPBlendMode), and composites the result
// with xpCompositeBlendPass. The coefficient modes (0-14) never call xpBlendColors and take no
// extra pass.
//
// The arithmetic is the one applyPorterDuffXP performs: the same blend inputs, the same coverage
// composite, the same zero-coverage discard. Only the dst read moves ahead of the discard, which
// reads one value no fragment output depends on.
//
// Include after xp_porter_duff.inc and xp_porter_duff_fbf.inc.
#ifndef TGFX_XP_BLEND_PASS_INC
#define TGFX_XP_BLEND_PASS_INC

#if HAS_XP >= 1

vec4 xpReadDstColor() {
#if HAS_XP == 1
  highp vec2 dstCoord = (gl_FragCoord.xy - DstTextureUpperLeft) * DstTextureCoordScale;
  return texture(DstTextureSampler, dstCoord);
#else
  return subpassLoad(tgfx_SubpassInput);
#endif
}

// Whether the evaluation loop runs the extra blend pass.
bool xpNeedsBlendPass() {
  return XPBlendMode >= XP_FIRST_ADVANCED_MODE;
}

// applyPorterDuffXP with the dst read ahead and the advanced blend computed by the loop's pass
// (the coefficient modes return before reading it).
vec4 xpCompositeBlendPass(vec4 S, vec4 D, vec4 C, vec4 loopBlend) {
#if HAS_XP == 1
  // The zero-coverage discard of applyPorterDuffXP (see xp_porter_duff.inc for why it is
  // required).
  if (C.r <= 0.0 && C.g <= 0.0 && C.b <= 0.0) {
    discard;
  }
#endif
  int mode = XPBlendMode;
  XP_COMPOSITE_WITH_COVERAGE(loopBlend)
}

#endif

#endif
