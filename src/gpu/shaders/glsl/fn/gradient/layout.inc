//@function gradientLayoutT
float gradientLayoutT(int layoutType, float bias, float scale, vec2 coord) {
  if (layoutType == 1) {
    // Radial: t = length
    return length(coord);
  } else if (layoutType == 2) {
    // Conic: t = angle-based
    float angle = atan(-coord.y, -coord.x);
    return ((angle * 0.15915494309180001 + 0.5) + bias) * scale;
  } else if (layoutType == 3) {
    // Diamond: t = max(|x|, |y|)
    return max(abs(coord.x), abs(coord.y));
  }
  // Linear (layoutType == 0): t = x
  return coord.x + 1.0000000000000001e-05;
}
