// YUVTexture fragment hook: the three-plane read and the YUV-to-RGB conversion (the simplified
// former YUVTextureFillShader; the pointwise tail is stripped). The expressions are verbatim from
// the old kernel's main.
void YUVTexture_fragment(inout Fragment f) {
  highp vec2 texCoord = YUVTexture_vCoord.xy / YUVTexture_vCoord.z;
  highp vec2 finalCoord = clamp(texCoord, YUVTexture_Subset.xy, YUVTexture_Subset.zw);

  vec3 yuv;
  yuv.x = texture(YUVTexture_SamplerY, finalCoord).r;
#if YUVTexture_FORMAT == 0
  yuv.y = texture(YUVTexture_SamplerUV, finalCoord).r;
  yuv.z = texture(YUVTexture_SamplerV, finalCoord).r;
#else
  // The UV plane is an RG_88 texture; .rg reads (U, V) directly on every backend's plain binding.
  yuv.yz = texture(YUVTexture_SamplerUV, finalCoord).rg;
#endif
  if (YUVTexture_LimitedRange != 0) {
    yuv.x -= (16.0 / 255.0);
  }
  yuv.yz -= vec2(0.5, 0.5);
  vec3 rgb = clamp(YUVTexture_ColorConversion * yuv, 0.0, 1.0);
  vec4 color;
  if (YUVTexture_HasRgbaaa != 0) {
    highp vec2 alphaVertexColor = finalCoord + YUVTexture_AlphaStart;
    float yuv_a = texture(YUVTexture_SamplerY, alphaVertexColor).r;
    yuv_a = (yuv_a - 16.0 / 255.0) / (219.0 / 255.0 - 1.0 / 255.0);
    yuv_a = clamp(yuv_a, 0.0, 1.0);
    color = vec4(rgb * yuv_a, yuv_a);
  } else {
    color = vec4(rgb, 1.0);
  }
  f.color = color * f.color.a;
}
