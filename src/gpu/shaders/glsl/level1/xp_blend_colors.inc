// PorterDuff blend math shared by the xfer processors and the fused pointwise-chain kernel.
// xpBlendColors(S, D, mode) blends two premultiplied colors with a runtime-selected mode. This
// file has its own include guard so kernels that need the blend math without the dst-texture XP
// machinery (PointwiseChainShader) can include it directly.
//
// The heavy modes (Overlay, ColorDodge, ColorBurn, HardLight, SoftLight, and the HSL family) are
// written as branch-free vector selections of the runtime's per-component formulas
// (GLSLBlend.cpp): each lane evaluates every arm and mix() with a bvec picks the arm the scalar
// branch would have taken. mix() with a boolean vector is a true component-wise select (SPIR-V
// OpSelect), so unselected lanes are discarded bit-for-bit. Division denominators are sanitized
// (zero replaced with one) only on lanes whose result the select discards, keeping every
// evaluated lane finite; the per-lane arithmetic mirrors the original expression trees exactly,
// so selected lanes produce bit-identical results.
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
  float lowDen = (outLum != minComp) ? (outLum - minComp) : 1.0;
  vec3 lowClamped = vec3(outLum) + ((outColor - vec3(outLum)) * outLum) / vec3(lowDen);
  outColor = mix(outColor, lowClamped, bvec3(minComp < 0.0 && outLum != minComp));
  float highDen = (maxComp != outLum) ? (maxComp - outLum) : 1.0;
  vec3 highClamped = vec3(outLum) + ((outColor - vec3(outLum)) * (alpha - outLum)) / vec3(highDen);
  return mix(outColor, highClamped, bvec3(maxComp > alpha && maxComp != outLum));
}

vec3 xpSetSaturation(vec3 hueLumColor, vec3 satColor) {
  // Component-wise rank form of the classic min/mid/max permutation tree: every channel maps to
  // sat * (c - min) / (max - min), which is exactly the tree's mid-channel expression, while the
  // min channel evaluates to 0.0 and the max channel is replaced with the constant sat the tree
  // assigns to it. When max == min the tree returns all zeros; the sanitized denominator makes
  // every lane compute sat * 0 = 0 instead.
  float sat = xpSaturation(satColor);
  float minComp = min(min(hueLumColor.r, hueLumColor.g), hueLumColor.b);
  float maxComp = max(max(hueLumColor.r, hueLumColor.g), hueLumColor.b);
  float denom = (maxComp > minComp) ? (maxComp - minComp) : 1.0;
  vec3 ranked = (sat * (hueLumColor - vec3(minComp))) / vec3(denom);
  return mix(ranked, vec3(sat), equal(hueLumColor, vec3(maxComp)));
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
    vec3 fast = 2.0 * S.rgb * D.rgb;
    vec3 slow = vec3(S.a * D.a) - 2.0 * (vec3(S.a) - S.rgb) * (vec3(D.a) - D.rgb);
    result.rgb = mix(fast, slow, lessThan(2.0 * D.rgb, vec3(D.a)));
    result.rgb += D.rgb * (1.0 - S.a) + S.rgb * (1.0 - D.a);
  } else if (mode == 16) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
  } else if (mode == 17) {
    result.a = S.a + (1.0 - S.a) * D.a;
    result.rgb = max((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
  } else if (mode == 18) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec3 d = vec3(S.a) - S.rgb;
    vec3 dSafe = mix(vec3(1.0), d, notEqual(d, vec3(0.0)));
    vec3 dd = min(vec3(D.a), D.rgb * vec3(S.a) / dSafe);
    vec3 valElse = dd * vec3(S.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
    vec3 valZeroD = vec3(S.a * D.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
    vec3 val = mix(valElse, valZeroD, equal(d, vec3(0.0)));
    result.rgb = mix(val, S.rgb * (1.0 - D.a), equal(D.rgb, vec3(0.0)));
  } else if (mode == 19) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec3 sSafe = mix(vec3(1.0), S.rgb, notEqual(S.rgb, vec3(0.0)));
    vec3 dd = max(vec3(0.0), vec3(D.a) - (vec3(D.a) - D.rgb) * vec3(S.a) / sSafe);
    vec3 valElse = vec3(S.a) * dd + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
    vec3 valSrc0 = D.rgb * (1.0 - S.a);
    vec3 valFull = vec3(S.a * D.a) + S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
    vec3 val = mix(valElse, valSrc0, equal(S.rgb, vec3(0.0)));
    result.rgb = mix(val, valFull, equal(D.rgb, vec3(D.a)));
  } else if (mode == 20) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec3 fast = 2.0 * S.rgb * D.rgb;
    vec3 slow = vec3(S.a * D.a) - 2.0 * (vec3(D.a) - D.rgb) * (vec3(S.a) - S.rgb);
    result.rgb = mix(fast, slow, lessThan(2.0 * S.rgb, vec3(S.a)));
    result.rgb += S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
  } else if (mode == 21) {
    result.a = S.a + (1.0 - S.a) * D.a;
    if (D.a == 0.0) {
      result.rgb = S.rgb;
    } else {
      vec3 sa2 = S.a - 2.0 * S.rgb;
      vec3 arm1 = (D.rgb * D.rgb * sa2) / vec3(D.a) + (1.0 - D.a) * S.rgb + D.rgb * (1.0 - sa2);
      vec3 DSqd = D.rgb * D.rgb;
      vec3 DCub = DSqd * D.rgb;
      float DaSqd = D.a * D.a;
      float DaCub = DaSqd * D.a;
      vec3 arm2 = (vec3(DaSqd) * (S.rgb - D.rgb * (3.0 * S.a - 6.0 * S.rgb - 1.0)) +
                   12.0 * D.a * DSqd * sa2 - 16.0 * DCub * sa2 - vec3(DaCub) * S.rgb) /
                  vec3(DaSqd);
      vec3 arm3 = D.rgb * (sa2 + 1.0) + S.rgb - sqrt(vec3(D.a) * D.rgb) * sa2 - vec3(D.a) * S.rgb;
      vec3 val = mix(arm3, arm2, lessThanEqual(4.0 * D.rgb, vec3(D.a)));
      result.rgb = mix(val, arm1, lessThanEqual(2.0 * S.rgb, vec3(S.a)));
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
    result.rgb = xpSetLuminance(xpSetSaturation(S.rgb * D.a, dstSrcAlpha.rgb), dstSrcAlpha.a,
                                dstSrcAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  } else if (mode == 26) {
    result.a = S.a + (1.0 - S.a) * D.a;
    vec4 dstSrcAlpha = D * S.a;
    result.rgb = xpSetLuminance(xpSetSaturation(dstSrcAlpha.rgb, S.rgb * D.a), dstSrcAlpha.a,
                                dstSrcAlpha.rgb);
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
