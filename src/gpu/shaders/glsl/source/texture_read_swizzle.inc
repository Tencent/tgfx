vec4 aotReadSwizzle(vec4 color, int mode) {
  if (mode == 1) {
    return color.rrra;
  }
  if (mode == 2) {
    return color.rgrg;
  }
  return color;
}
