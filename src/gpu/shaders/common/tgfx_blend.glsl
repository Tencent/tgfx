// Copyright (C) 2026 Tencent. All rights reserved.
// tgfx_blend.glsl - GLSL implementations of all 15 advanced blend modes.
// Self-contained: no external includes needed.
// All math uses premultiplied alpha. Alpha channel uses src-over for all modes.
//
// Blend mode indices (matching BlendMode enum):
//   15=Overlay, 16=Darken, 17=Lighten, 18=ColorDodge, 19=ColorBurn,
//   20=HardLight, 21=SoftLight, 22=Difference, 23=Exclusion, 24=Multiply,
//   25=Hue, 26=Saturation, 27=Color, 28=Luminosity, 29=PlusDarker

///////////////////////////////////////////////////////////////////////////////
// HSL helper functions (used by Hue, Saturation, Color, Luminosity modes)
///////////////////////////////////////////////////////////////////////////////

float tgfx_luminance(vec3 color) {
  return dot(vec3(0.3, 0.59, 0.11), color);
}

vec3 tgfx_set_luminance(vec3 hueSat, float alpha, vec3 lumColor) {
  float diff = tgfx_luminance(lumColor - hueSat);
  vec3 outColor = hueSat + diff;
  float outLum = tgfx_luminance(outColor);
  float minComp = min(min(outColor.r, outColor.g), outColor.b);
  float maxComp = max(max(outColor.r, outColor.g), outColor.b);
  if (minComp < 0.0 && outLum != minComp) {
    outColor = outLum + ((outColor - vec3(outLum, outLum, outLum)) * outLum) / (outLum - minComp);
  }
  if (maxComp > alpha && maxComp != outLum) {
    outColor = outLum + ((outColor - vec3(outLum, outLum, outLum)) * (alpha - outLum)) / (maxComp - outLum);
  }
  return outColor;
}

float tgfx_saturation(vec3 color) {
  return max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
}

vec3 tgfx_set_saturation_helper(float minComp, float midComp, float maxComp, float sat) {
  if (minComp < maxComp) {
    vec3 result;
    result.r = 0.0;
    result.g = sat * (midComp - minComp) / (maxComp - minComp);
    result.b = sat;
    return result;
  } else {
    return vec3(0, 0, 0);
  }
}

vec3 tgfx_set_saturation(vec3 hueLumColor, vec3 satColor) {
  float sat = tgfx_saturation(satColor);
  if (hueLumColor.r <= hueLumColor.g) {
    if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.rgb = tgfx_set_saturation_helper(hueLumColor.r, hueLumColor.g, hueLumColor.b, sat);
    } else if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.rbg = tgfx_set_saturation_helper(hueLumColor.r, hueLumColor.b, hueLumColor.g, sat);
    } else {
      hueLumColor.brg = tgfx_set_saturation_helper(hueLumColor.b, hueLumColor.r, hueLumColor.g, sat);
    }
  } else if (hueLumColor.r <= hueLumColor.b) {
    hueLumColor.grb = tgfx_set_saturation_helper(hueLumColor.g, hueLumColor.r, hueLumColor.b, sat);
  } else if (hueLumColor.g <= hueLumColor.b) {
    hueLumColor.gbr = tgfx_set_saturation_helper(hueLumColor.g, hueLumColor.b, hueLumColor.r, sat);
  } else {
    hueLumColor.bgr = tgfx_set_saturation_helper(hueLumColor.b, hueLumColor.g, hueLumColor.r, sat);
  }
  return hueLumColor;
}

///////////////////////////////////////////////////////////////////////////////
// Per-component helper functions
///////////////////////////////////////////////////////////////////////////////

// HardLight per-component blend (used by HardLight and Overlay).
// Computes hard light for RGB, then adds the uncovered portions.
vec3 tgfx_hard_light_rgb(vec4 src, vec4 dst) {
  vec3 result;
  // Per-component hard light core
  for (int i = 0; i < 3; i++) {
    float s = (i == 0) ? src.r : ((i == 1) ? src.g : src.b);
    float d = (i == 0) ? dst.r : ((i == 1) ? dst.g : dst.b);
    float f;
    if (2.0 * s < src.a) {
      f = 2.0 * s * d;
    } else {
      f = src.a * dst.a - 2.0 * (dst.a - d) * (src.a - s);
    }
    if (i == 0) result.r = f;
    else if (i == 1) result.g = f;
    else result.b = f;
  }
  // Add uncovered portions
  result.rgb += src.rgb * (1.0 - dst.a) + dst.rgb * (1.0 - src.a);
  return result;
}

// ColorDodge per-component
float tgfx_color_dodge_component(float sc, float sa, float dc, float da) {
  if (0.0 == dc) {
    return sc * (1.0 - da);
  } else {
    float d = sa - sc;
    if (0.0 == d) {
      return sa * da + sc * (1.0 - da) + dc * (1.0 - sa);
    } else {
      d = min(da, dc * sa / d);
      return d * sa + sc * (1.0 - da) + dc * (1.0 - sa);
    }
  }
}

// ColorBurn per-component
float tgfx_color_burn_component(float sc, float sa, float dc, float da) {
  if (da == dc) {
    return sa * da + sc * (1.0 - da) + dc * (1.0 - sa);
  } else if (0.0 == sc) {
    return dc * (1.0 - sa);
  } else {
    float d = max(0.0, da - (da - dc) * sa / sc);
    return sa * d + sc * (1.0 - da) + dc * (1.0 - sa);
  }
}

// SoftLight per-component (assumes dst.a > 0)
float tgfx_soft_light_component(float sc, float sa, float dc, float da) {
  // if (2S <= Sa)
  if (2.0 * sc <= sa) {
    // (D^2 (Sa-2S))/Da + (1-Da)S + D(-Sa+2S+1)
    return (dc * dc * (sa - 2.0 * sc)) / da +
           (1.0 - da) * sc + dc * (-sa + 2.0 * sc + 1.0);
  // else if (4D <= Da)
  } else if (4.0 * dc <= da) {
    float DSqd = dc * dc;
    float DCub = DSqd * dc;
    float DaSqd = da * da;
    float DaCub = DaSqd * da;
    // (Da^2(S - D(3Sa-6S-1)) + 12DaD^2(Sa-2S) - 16D^3(Sa-2S) - Da^3 S) / Da^2
    return (DaSqd * (sc - dc * (3.0 * sa - 6.0 * sc - 1.0)) +
            12.0 * da * DSqd * (sa - 2.0 * sc) - 16.0 * DCub * (sa - 2.0 * sc) -
            DaCub * sc) / DaSqd;
  } else {
    // -sqrt(Da*D)(Sa-2S) - DaS + D(Sa-2S+1) + S
    return dc * (sa - 2.0 * sc + 1.0) + sc -
           sqrt(da * dc) * (sa - 2.0 * sc) - da * sc;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Individual blend mode functions
///////////////////////////////////////////////////////////////////////////////

vec4 tgfx_blend_overlay(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  // Overlay is HardLight with src and dst swapped
  result.rgb = tgfx_hard_light_rgb(dst, src);
  return result;
}

vec4 tgfx_blend_darken(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = min((1.0 - src.a) * dst.rgb + src.rgb, (1.0 - dst.a) * src.rgb + dst.rgb);
  return result;
}

vec4 tgfx_blend_lighten(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = max((1.0 - src.a) * dst.rgb + src.rgb, (1.0 - dst.a) * src.rgb + dst.rgb);
  return result;
}

vec4 tgfx_blend_color_dodge(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.r = tgfx_color_dodge_component(src.r, src.a, dst.r, dst.a);
  result.g = tgfx_color_dodge_component(src.g, src.a, dst.g, dst.a);
  result.b = tgfx_color_dodge_component(src.b, src.a, dst.b, dst.a);
  return result;
}

vec4 tgfx_blend_color_burn(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.r = tgfx_color_burn_component(src.r, src.a, dst.r, dst.a);
  result.g = tgfx_color_burn_component(src.g, src.a, dst.g, dst.a);
  result.b = tgfx_color_burn_component(src.b, src.a, dst.b, dst.a);
  return result;
}

vec4 tgfx_blend_hard_light(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = tgfx_hard_light_rgb(src, dst);
  return result;
}

vec4 tgfx_blend_soft_light(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  if (0.0 == dst.a) {
    result.rgba = src;
  } else {
    result.r = tgfx_soft_light_component(src.r, src.a, dst.r, dst.a);
    result.g = tgfx_soft_light_component(src.g, src.a, dst.g, dst.a);
    result.b = tgfx_soft_light_component(src.b, src.a, dst.b, dst.a);
  }
  return result;
}

vec4 tgfx_blend_difference(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = src.rgb + dst.rgb - 2.0 * min(src.rgb * dst.a, dst.rgb * src.a);
  return result;
}

vec4 tgfx_blend_exclusion(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = dst.rgb + src.rgb - 2.0 * dst.rgb * src.rgb;
  return result;
}

vec4 tgfx_blend_multiply(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb + src.rgb * dst.rgb;
  return result;
}

vec4 tgfx_blend_hue(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 dstSrcAlpha = dst * src.a;
  result.rgb = tgfx_set_luminance(tgfx_set_saturation(src.rgb * dst.a, dstSrcAlpha.rgb), dstSrcAlpha.a, dstSrcAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_saturation(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 dstSrcAlpha = dst * src.a;
  result.rgb = tgfx_set_luminance(tgfx_set_saturation(dstSrcAlpha.rgb, src.rgb * dst.a), dstSrcAlpha.a, dstSrcAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_color(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 srcDstAlpha = src * dst.a;
  result.rgb = tgfx_set_luminance(srcDstAlpha.rgb, srcDstAlpha.a, dst.rgb * src.a);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_luminosity(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  vec4 srcDstAlpha = src * dst.a;
  result.rgb = tgfx_set_luminance(dst.rgb * src.a, srcDstAlpha.a, srcDstAlpha.rgb);
  result.rgb += (1.0 - src.a) * dst.rgb + (1.0 - dst.a) * src.rgb;
  return result;
}

vec4 tgfx_blend_plus_darker(vec4 src, vec4 dst) {
  vec4 result;
  result.a = src.a + (1.0 - src.a) * dst.a;
  result.rgb = clamp(1.0 + src.rgb + dst.rgb - dst.a - src.a, 0.0, 1.0);
  result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;
  return result;
}

///////////////////////////////////////////////////////////////////////////////
// Main dispatch function
///////////////////////////////////////////////////////////////////////////////

vec4 tgfx_blend(vec4 src, vec4 dst, int mode) {
  if (mode == 15) return tgfx_blend_overlay(src, dst);
  if (mode == 16) return tgfx_blend_darken(src, dst);
  if (mode == 17) return tgfx_blend_lighten(src, dst);
  if (mode == 18) return tgfx_blend_color_dodge(src, dst);
  if (mode == 19) return tgfx_blend_color_burn(src, dst);
  if (mode == 20) return tgfx_blend_hard_light(src, dst);
  if (mode == 21) return tgfx_blend_soft_light(src, dst);
  if (mode == 22) return tgfx_blend_difference(src, dst);
  if (mode == 23) return tgfx_blend_exclusion(src, dst);
  if (mode == 24) return tgfx_blend_multiply(src, dst);
  if (mode == 25) return tgfx_blend_hue(src, dst);
  if (mode == 26) return tgfx_blend_saturation(src, dst);
  if (mode == 27) return tgfx_blend_color(src, dst);
  if (mode == 28) return tgfx_blend_luminosity(src, dst);
  if (mode == 29) return tgfx_blend_plus_darker(src, dst);
  return src; // fallback
}
