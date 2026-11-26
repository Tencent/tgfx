// The clamped gradient's boundary fallback: inside the ramp the colorizer's value, outside it
// the border colors, and a fully clamped ramp (t.y < 0) fades to transparent. The result is
// premultiplied and modulated by the input color's alpha, matching the text route's epilogue.
// The branch lives in the function body: the IR route only records the call.
//@function tgfxClampedGradient
//@op ClampedGradient
//@frag
vec4 tgfxClampedGradient(vec4 t /*@in*/, vec4 colorized /*@in*/, vec4 inputColor /*@in*/,
                         vec4 leftBorder, vec4 rightBorder) {
  vec4 c = colorized;
  if (t.y < 0.0) {
    c = vec4(0.0);
  } else if (t.x <= 0.0) {
    c = leftBorder;
  } else if (t.x >= 1.0) {
    c = rightBorder;
  }
  c.rgb *= c.a;
  return c * inputColor.a;
}
