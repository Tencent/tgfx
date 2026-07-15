// PorterDuff XferProcessor support (Framebuffer Fetch mode).
// Include this file after xp_porter_duff.inc.
// When HAS_XP == 2, provides applyPorterDuffXP() using subpassInput to read framebuffer color.
// Requires:
//   HAS_XP defined as 2
//   XPBlendMode declared in FragmentUniformBlock
//   xpBlendColors() available from xp_porter_duff.inc

#if HAS_XP == 2

layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput tgfx_SubpassInput;

vec4 applyPorterDuffXP(vec4 srcColor, vec4 coverage) {
  vec4 dstColor = subpassLoad(tgfx_SubpassInput);
  vec4 blended = xpBlendColors(srcColor, dstColor, XPBlendMode);
  return coverage * blended + (vec4(1.0) - coverage) * dstColor;
}

#endif
