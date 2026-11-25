// The YUV-to-RGB conversion: the range bias, the color matrix, and the alpha channel's own
// range normalization. The raw alpha arrives as a parameter so the call site keeps only the
// sampling orchestration on its side.
//@function tgfxYUVToRGB
//@op YUVToRGB
//@frag
vec4 tgfxYUVToRGB(highp vec3 yuv /*@in*/, highp mat3 colorXform /*@in*/, int limitedRange /*@in*/, int hasAlpha /*@in*/, highp float rawAlpha /*@in*/) {
  if (limitedRange != 0) {
    yuv.x -= (16.0 / 255.0);
  }
  yuv.yz -= vec2(0.5, 0.5);
  highp vec3 rgb = clamp(colorXform * yuv, 0.0, 1.0);
  if (hasAlpha != 0) {
    highp float alpha = clamp((rawAlpha - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0), 0.0, 1.0);
    return vec4(rgb * alpha, alpha);
  }
  return vec4(rgb, 1.0);
}
