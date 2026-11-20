//@function pmApplyYUV Yuv
vec4 pmApplyYUV(vec4 in0 /*@in*/, vec3 coords /*@in*/, vec4 subset, mat3 colorConversion,
                int limitedRange, vec2 alphaStart, int hasRgbaaa, int format,
                sampler2D planeY /*@in*/, sampler2D planeUV /*@in*/, sampler2D planeV /*@in*/) {
  // The multi-plane source: format 0 is three planes (Y, U, V), anything else reads the
  // interleaved RG plane. The expressions are the YUVTexture kernel's, verbatim.
  highp vec2 texCoord = coords.xy / coords.z;
  highp vec2 finalCoord = clamp(texCoord, subset.xy, subset.zw);
  vec3 yuv;
  yuv.x = texture(planeY, finalCoord).r;
  if (format == 0) {
    yuv.y = texture(planeUV, finalCoord).r;
    yuv.z = texture(planeV, finalCoord).r;
  } else {
    yuv.yz = texture(planeUV, finalCoord).rg;
  }
  if (limitedRange != 0) {
    yuv.x -= (16.0 / 255.0);
  }
  yuv.yz -= vec2(0.5, 0.5);
  vec3 rgb = clamp(colorConversion * yuv, 0.0, 1.0);
  if (hasRgbaaa != 0) {
    // The RGBAAA layout's alpha plane rides the Y sampler at an offset.
    highp vec2 alphaCoord = finalCoord + alphaStart;
    float yuv_a = texture(planeY, alphaCoord).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    return vec4(rgb, 1.0) * yuv_a * in0.a;
  }
  return vec4(rgb, 1.0) * in0.a;
}
