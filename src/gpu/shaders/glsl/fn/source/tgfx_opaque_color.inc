// The opaque form a blend's operands read: the color's channels with an alpha of one, so the
// blend math sees a fully covered source. One function so the value is not a free-text local the
// bytecode routes cannot name.
//@function tgfxOpaqueColor
vec4 tgfxOpaqueColor(vec4 color) {
  return vec4(color.rgb, 1.0);
}
