// PorterDuff blend math shared by the xfer processors and the fused pointwise-chain kernel.
// xpBlendColors(S, D, mode) blends two premultiplied colors with a runtime-selected mode. This
// file has its own include guard so kernels that need the blend math without the dst-texture XP
// machinery (PointwiseChainShader) can include it directly.
#ifndef TGFX_XP_BLEND_COLORS_INC
#define TGFX_XP_BLEND_COLORS_INC

float xpLuminance(vec3 c) {
  return dot(vec3(0.3, 0.59, 0.11), c);
}

float xpSaturation(vec3 c) {
  return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

vec3 xpSetLuminance(vec3 hueSat, float alpha, vec3 lumColor) {
  float diff = xpLuminance(lumColor) - xpLuminance(hueSat);
  vec3 outColor = hueSat + diff;
  float outLum = xpLuminance(outColor);
  float minComp = min(min(outColor.r, outColor.g), outColor.b);
  float maxComp = max(max(outColor.r, outColor.g), outColor.b);
  if (minComp < 0.0 && outLum != minComp) {
    outColor = vec3(outLum) + ((outColor - vec3(outLum)) * outLum) / (outLum - minComp);
  }
  if (maxComp > alpha && maxComp != outLum) {
    outColor = vec3(outLum) + ((outColor - vec3(outLum)) * (alpha - outLum)) / (maxComp - outLum);
  }
  return outColor;
}

vec3 xpSetSatHelper(float minComp, float midComp, float maxComp, float sat) {
  if (minComp < maxComp) {
    return vec3(0.0, sat * (midComp - minComp) / (maxComp - minComp), sat);
  }
  return vec3(0.0);
}

vec3 xpSetSaturation(vec3 hueLumColor, vec3 satColor) {
  float sat = xpSaturation(satColor);
  if (hueLumColor.r <= hueLumColor.g) {
    if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.rgb = xpSetSatHelper(hueLumColor.r, hueLumColor.g, hueLumColor.b, sat);
    } else if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.rbg = xpSetSatHelper(hueLumColor.r, hueLumColor.b, hueLumColor.g, sat);
    } else {
      hueLumColor.brg = xpSetSatHelper(hueLumColor.b, hueLumColor.r, hueLumColor.g, sat);
    }
  } else {
    if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.grb = xpSetSatHelper(hueLumColor.g, hueLumColor.r, hueLumColor.b, sat);
    } else if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.gbr = xpSetSatHelper(hueLumColor.g, hueLumColor.b, hueLumColor.r, sat);
    } else {
      hueLumColor.bgr = xpSetSatHelper(hueLumColor.b, hueLumColor.g, hueLumColor.r, sat);
    }
  }
  return hueLumColor;
}

vec4 xpBlendColors(vec4 S, vec4 D, int mode) {
  vec4 result;
  if (mode == 0) {
    result = vec4(0.0);
  } else if (mode == 1) {
    result = S;
  } else if (mode == 2) {
    result = D;
  } else if (mode == 3) {
    result = S + D * (1.0 - S.a);
  } else if (mode == 4) {
    result = S * (1.0 - D.a) + D;
  } else if (mode == 5) {
    result = S * D.a;
  } else if (mode == 6) {
    result = D * S.a;
  } else if (mode == 7) {
    result = S * (1.0 - D.a);
  } else if (mode == 8) {
    result = D * (1.0 - S.a);
  } else if (mode == 9) {
    result = S * D.a + D * (1.0 - S.a);
  } else if (mode == 10) {
    result = S * (1.0 - D.a) + D * S.a;
  } else if (mode == 11) {
    result = S * (1.0 - D.a) + D * (1.0 - S.a);
  } else if (mode == 12) {
    result = clamp(S + D, 0.0, 1.0);
  } else if (mode == 13) {
    result = S * D;
  } else if (mode == 14) {
    result = S + D - S * D;
  } else if (mode == 15) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.r = (2.0 * D.r < D.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (S.a - S.r) * (D.a - D.r);
    result.g = (2.0 * D.g < D.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (S.a - S.g) * (D.a - D.g);
    result.b = (2.0 * D.b < D.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (S.a - S.b) * (D.a - D.b);
    result.rgb += D.rgb * (1.0 - S.a) + S.rgb * (1.0 - D.a);
  } else if (mode == 16) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
  } else if (mode == 17) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = max((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
  } else if (mode == 18) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec3 r3;
    for (int c = 0; c < 3; ++c) {
      float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
      float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
      float val;
      if (Dc == 0.0) {
        val = Sc * (1.0 - D.a);
      } else {
        float d = S.a - Sc;
        if (d == 0.0) {
          val = S.a * D.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a);
        } else {
          d = min(D.a, Dc * S.a / d);
          val = d * S.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a);
        }
      }
      if (c == 0) r3.r = val;
      else if (c == 1) r3.g = val;
      else r3.b = val;
    }
    result.rgb = r3;
  } else if (mode == 19) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec3 r3;
    for (int c = 0; c < 3; ++c) {
      float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
      float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
      float val;
      if (D.a == Dc) {
        val = S.a * D.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a);
      } else if (Sc == 0.0) {
        val = Dc * (1.0 - S.a);
      } else {
        float d = max(0.0, D.a - (D.a - Dc) * S.a / Sc);
        val = S.a * d + Sc * (1.0 - D.a) + Dc * (1.0 - S.a);
      }
      if (c == 0) r3.r = val;
      else if (c == 1) r3.g = val;
      else r3.b = val;
    }
    result.rgb = r3;
  } else if (mode == 20) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.r = (2.0 * S.r < S.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (D.a - D.r) * (S.a - S.r);
    result.g = (2.0 * S.g < S.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (D.a - D.g) * (S.a - S.g);
    result.b = (2.0 * S.b < S.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (D.a - D.b) * (S.a - S.b);
    result.rgb += S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  } else if (mode == 21) {
    result.a = S.a + (1.0 - S.a) * D.a;
    if (D.a == 0.0) {
      result.rgb = S.rgb;
    } else {
      vec3 r3;
      for (int c = 0; c < 3; ++c) {
        float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
        float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
        float val;
        if (2.0 * Sc <= S.a) {
          val = (Dc * Dc * (S.a - 2.0 * Sc)) / D.a + (1.0 - D.a) * Sc + Dc * (-S.a + 2.0 * Sc + 1.0);
        } else if (4.0 * Dc <= D.a) {
          float DSqd = Dc * Dc;
          float DCub = DSqd * Dc;
          float DaSqd = D.a * D.a;
          float DaCub = DaSqd * D.a;
          val = (DaSqd * (Sc - Dc * (3.0 * S.a - 6.0 * Sc - 1.0)) + 12.0 * D.a * DSqd * (S.a - 2.0 * Sc) - 16.0 * DCub * (S.a - 2.0 * Sc) - DaCub * Sc) / DaSqd;
        } else {
          val = Dc * (S.a - 2.0 * Sc + 1.0) + Sc - sqrt(D.a * Dc) * (S.a - 2.0 * Sc) - D.a * Sc;
        }
        if (c == 0) r3.r = val;
        else if (c == 1) r3.g = val;
        else r3.b = val;
      }
      result.rgb = r3;
    }
  } else if (mode == 22) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = S.rgb + D.rgb - 2.0 * min(S.rgb * D.a, D.rgb * S.a);
  } else if (mode == 23) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = D.rgb + S.rgb - 2.0 * D.rgb * S.rgb;
  } else if (mode == 24) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb + S.rgb * D.rgb;
  } else if (mode == 25) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec4 dstSrcAlpha = D * S.a;
    result.rgb = xpSetLuminance(xpSetSaturation(S.rgb * D.a, dstSrcAlpha.rgb), dstSrcAlpha.a, dstSrcAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  } else if (mode == 26) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec4 dstSrcAlpha = D * S.a;
    result.rgb = xpSetLuminance(xpSetSaturation(dstSrcAlpha.rgb, S.rgb * D.a), dstSrcAlpha.a, dstSrcAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  } else if (mode == 27) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec4 srcDstAlpha = S * D.a;
    result.rgb = xpSetLuminance(srcDstAlpha.rgb, srcDstAlpha.a, D.rgb * S.a);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  } else if (mode == 28) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec4 srcDstAlpha = S * D.a;
    result.rgb = xpSetLuminance(D.rgb * S.a, srcDstAlpha.a, srcDstAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  } else if (mode == 29) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = clamp(1.0 + S.rgb + D.rgb - D.a - S.a, 0.0, 1.0);
    result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;
  } else {
    result = S;
  }
  return result;
}

// Coverage application for the XferProcessor path. Modes 0-14 (Clear..Screen) are coefficient
// modes: the runtime bakes the coverage into the BlendFormula table (GLSLBlend.cpp Coeffs[1]) and
// emits mode-specific forms like S*C + D*(1-C.a) for Src, which differ from the generic
// C*blend(S,D) + (1-C)*D whenever the coverage alpha does not equal its rgb or S.a < 1. These
// branches mirror that table exactly (primary/secondary outputs and src/dst factors, final
// clamp included); advanced modes keep the generic form, matching the runtime's
// HandleBlendModes + coverage wrapper.
vec4 xpBlendWithCoverage(vec4 S, vec4 D, vec4 C, int mode) {
  if (mode == 0) {
    // clear: D*One - C*D
    return clamp(D - C * D, 0.0, 1.0);
  }
  if (mode == 1) {
    // src: primary=S*C; dst factor OneMinusSrc1Alpha on secondary=C
    return clamp(S * C + D * (1.0 - C.a), 0.0, 1.0);
  }
  if (mode == 2) {
    // dst
    return clamp(D, 0.0, 1.0);
  }
  if (mode == 3) {
    // src-over: primary=S*C; dst factor OneMinusSrcAlpha on primary
    return clamp(S * C + D * (1.0 - (S * C).a), 0.0, 1.0);
  }
  if (mode == 4) {
    // dst-over: src factor OneMinusDstAlpha on primary
    return clamp(S * C * (1.0 - D.a) + D, 0.0, 1.0);
  }
  if (mode == 5) {
    // src-in: src factor DstAlpha on primary; dst factor OneMinusSrc1Alpha on secondary=C
    return clamp(S * C * D.a + D * (1.0 - C.a), 0.0, 1.0);
  }
  if (mode == 6) {
    // dst-in: D*One - (1-S.a)*C*D
    return clamp(D - (1.0 - S.a) * C * D, 0.0, 1.0);
  }
  if (mode == 7) {
    // src-out: src factor OneMinusDstAlpha on primary; dst factor OneMinusSrc1Alpha on secondary
    return clamp(S * C * (1.0 - D.a) + D * (1.0 - C.a), 0.0, 1.0);
  }
  if (mode == 8) {
    // dst-out: dst factor OneMinusSrcAlpha on primary
    return clamp(D * (1.0 - (S * C).a), 0.0, 1.0);
  }
  if (mode == 9) {
    // src-atop
    return clamp(S * C * D.a + D * (1.0 - (S * C).a), 0.0, 1.0);
  }
  if (mode == 10) {
    // dst-atop: secondary=(1-S.a)*C; dst factor OneMinusSrc1 (vec4)
    return clamp(S * C * (1.0 - D.a) + D * (vec4(1.0) - (1.0 - S.a) * C), 0.0, 1.0);
  }
  if (mode == 11) {
    // xor
    return clamp(S * C * (1.0 - D.a) + D * (1.0 - (S * C).a), 0.0, 1.0);
  }
  if (mode == 12) {
    // plus
    return clamp(S * C + D, 0.0, 1.0);
  }
  if (mode == 13) {
    // modulate: D*One - (vec4(1.0)-S)*C*D
    return clamp(D - (vec4(1.0) - S) * C * D, 0.0, 1.0);
  }
  if (mode == 14) {
    // screen: dst factor OneMinusSrcColor (vec4) on primary
    return clamp(S * C + D * (vec4(1.0) - S * C), 0.0, 1.0);
  }
  vec4 blended = xpBlendColors(S, D, mode);
  return C * blended + (vec4(1.0) - C) * D;
}

#endif  // TGFX_XP_BLEND_COLORS_INC
