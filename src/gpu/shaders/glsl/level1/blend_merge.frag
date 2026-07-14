// BlendMergeShader fragment shader
// Permutation dimensions (frag):
//   CHILD_TYPE (0~2): 0=DstChild, 1=SrcChild, 2=TwoChild
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   CHILD0_MODE (0~2): 0=TextureEffect (sample from TextureSampler_0),
//                       1=ConstColor (use ChildConstColor uniform, no texture),
//                       2=TiledTextureEffect (tiling via runtime uniforms TileModeX/Y)
//
// BLEND_MODE is a runtime uniform (not a compile-time permutation dimension).
// TileModeX/TileModeY are runtime uniforms when CHILD0_MODE=2 (values 0~8, same as ShaderMode).
//
// When CHILD0_MODE=1 (ConstColor), the first child processor is a ConstColorProcessor that
// outputs a solid uniform color instead of sampling a texture. This is common for
// ModeColorFilter (blend a solid color with the input) and DropShadow effects.
//
// When CHILD0_MODE=2 (TiledTexture), the first child is a TiledTextureEffect that applies
// repeat/mirror/clamp-to-border tiling. The tiling mode per-axis is selected at runtime via
// TileModeX/TileModeY uniforms to avoid permutation explosion (9*9=81 combinations).
#version 450

#ifndef CHILD_TYPE
#define CHILD_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef CHILD0_MODE
#define CHILD0_MODE 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int BlendModeValue;
#if CHILD0_MODE == 1
  vec4 ChildConstColor;
#elif CHILD0_MODE == 2
  vec4 TiledSubset;
  vec4 TiledClamp;
  int TileModeX;
  int TileModeY;
#endif
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

#if CHILD0_MODE == 0 || CHILD0_MODE == 2
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
  #if CHILD_TYPE == 2
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
    #define XP_DST_TEX_BINDING 2
  #else
    #define XP_DST_TEX_BINDING 1
  #endif
#elif CHILD0_MODE == 1
  // ConstColor child: first texture binding slot is freed.
  #if CHILD_TYPE == 2
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_1;
    #define XP_DST_TEX_BINDING 1
  #else
    #define XP_DST_TEX_BINDING 0
  #endif
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

// --- Tiling helper functions (used when CHILD0_MODE == 2) ---
#if CHILD0_MODE == 2
float tileRepeat(float coord, float lo, float hi) {
  float len = hi - lo;
  return mod(coord - lo, len) + lo;
}

float tileMirror(float coord, float lo, float hi) {
  float len = hi - lo;
  float t = mod(coord - lo, 2.0 * len);
  return mix(lo + t, hi - (t - len), step(len, t));
}

float applyTileMode(float coord, int mode, float subLo, float subHi, float clampLo, float clampHi) {
  if (mode == 1) {
    return clamp(coord, clampLo, clampHi);
  } else if (mode == 2 || mode == 3 || mode == 4 || mode == 5) {
    return clamp(tileRepeat(coord, subLo, subHi), clampLo, clampHi);
  } else if (mode == 6) {
    return clamp(tileMirror(coord, subLo, subHi), clampLo, clampHi);
  } else if (mode == 8) {
    return clamp(coord, clampLo, clampHi);
  }
  return coord;
}
#endif

// --- Blend helper functions ---

// HSL helpers for Hue, Saturation, Color, Luminosity modes
float blendLuminance(vec3 c) {
  return dot(vec3(0.3, 0.59, 0.11), c);
}

float blendSaturation(vec3 c) {
  return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

vec3 blendSetLuminance(vec3 hueSat, float alpha, vec3 lumColor) {
  float diff = blendLuminance(lumColor) - blendLuminance(hueSat);
  vec3 outColor = hueSat + diff;
  float outLum = blendLuminance(outColor);
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

vec3 blendSetSatHelper(float minComp, float midComp, float maxComp, float sat) {
  if (minComp < maxComp) {
    return vec3(0.0, sat * (midComp - minComp) / (maxComp - minComp), sat);
  }
  return vec3(0.0);
}

vec3 blendSetSaturation(vec3 hueLumColor, vec3 satColor) {
  float sat = blendSaturation(satColor);
  if (hueLumColor.r <= hueLumColor.g) {
    if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.rgb = blendSetSatHelper(hueLumColor.r, hueLumColor.g, hueLumColor.b, sat);
    } else if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.rbg = blendSetSatHelper(hueLumColor.r, hueLumColor.b, hueLumColor.g, sat);
    } else {
      hueLumColor.brg = blendSetSatHelper(hueLumColor.b, hueLumColor.r, hueLumColor.g, sat);
    }
  } else {
    if (hueLumColor.r <= hueLumColor.b) {
      hueLumColor.grb = blendSetSatHelper(hueLumColor.g, hueLumColor.r, hueLumColor.b, sat);
    } else if (hueLumColor.g <= hueLumColor.b) {
      hueLumColor.gbr = blendSetSatHelper(hueLumColor.g, hueLumColor.b, hueLumColor.r, sat);
    } else {
      hueLumColor.bgr = blendSetSatHelper(hueLumColor.b, hueLumColor.g, hueLumColor.r, sat);
    }
  }
  return hueLumColor;
}

vec4 blendColors(vec4 S, vec4 D) {
  int mode = BlendModeValue;
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
  } else {
    result.a = S.a + (1.0 - S.a) * D.a;
    if (mode == 15) {
      result.r = (2.0 * D.r < D.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (S.a - S.r) * (D.a - D.r);
      result.g = (2.0 * D.g < D.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (S.a - S.g) * (D.a - D.g);
      result.b = (2.0 * D.b < D.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (S.a - S.b) * (D.a - D.b);
      result.rgb += D.rgb * (1.0 - S.a) + S.rgb * (1.0 - D.a);
    } else if (mode == 16) {
      result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
    } else if (mode == 17) {
      result.rgb = max((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
    } else if (mode == 18) {
      vec3 r3;
      for (int c = 0; c < 3; ++c) {
        float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
        float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
        float val;
        if (Dc == 0.0) { val = Sc * (1.0 - D.a); }
        else { float d = S.a - Sc; if (d == 0.0) { val = S.a * D.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a); } else { d = min(D.a, Dc * S.a / d); val = d * S.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a); } }
        if (c == 0) r3.r = val; else if (c == 1) r3.g = val; else r3.b = val;
      }
      result.rgb = r3;
    } else if (mode == 19) {
      vec3 r3;
      for (int c = 0; c < 3; ++c) {
        float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
        float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
        float val;
        if (D.a == Dc) { val = S.a * D.a + Sc * (1.0 - D.a) + Dc * (1.0 - S.a); }
        else if (Sc == 0.0) { val = Dc * (1.0 - S.a); }
        else { float d = max(0.0, D.a - (D.a - Dc) * S.a / Sc); val = S.a * d + Sc * (1.0 - D.a) + Dc * (1.0 - S.a); }
        if (c == 0) r3.r = val; else if (c == 1) r3.g = val; else r3.b = val;
      }
      result.rgb = r3;
    } else if (mode == 20) {
      result.r = (2.0 * S.r < S.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (D.a - D.r) * (S.a - S.r);
      result.g = (2.0 * S.g < S.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (D.a - D.g) * (S.a - S.g);
      result.b = (2.0 * S.b < S.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (D.a - D.b) * (S.a - S.b);
      result.rgb += S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
    } else if (mode == 21) {
      if (D.a == 0.0) { result = S; }
      else {
        vec3 r3;
        for (int c = 0; c < 3; ++c) {
          float Sc = (c == 0) ? S.r : ((c == 1) ? S.g : S.b);
          float Dc = (c == 0) ? D.r : ((c == 1) ? D.g : D.b);
          float val;
          if (2.0 * Sc <= S.a) { val = (Dc * Dc * (S.a - 2.0 * Sc)) / D.a + (1.0 - D.a) * Sc + Dc * (-S.a + 2.0 * Sc + 1.0); }
          else if (4.0 * Dc <= D.a) { float DSqd = Dc * Dc; float DCub = DSqd * Dc; float DaSqd = D.a * D.a; float DaCub = DaSqd * D.a; val = (DaSqd * (Sc - Dc * (3.0 * S.a - 6.0 * Sc - 1.0)) + 12.0 * D.a * DSqd * (S.a - 2.0 * Sc) - 16.0 * DCub * (S.a - 2.0 * Sc) - DaCub * Sc) / DaSqd; }
          else { val = Dc * (S.a - 2.0 * Sc + 1.0) + Sc - sqrt(D.a * Dc) * (S.a - 2.0 * Sc) - D.a * Sc; }
          if (c == 0) r3.r = val; else if (c == 1) r3.g = val; else r3.b = val;
        }
        result.rgb = r3;
      }
    } else if (mode == 22) {
      result.rgb = S.rgb + D.rgb - 2.0 * min(S.rgb * D.a, D.rgb * S.a);
    } else if (mode == 23) {
      result.rgb = D.rgb + S.rgb - 2.0 * D.rgb * S.rgb;
    } else if (mode == 24) {
      result.rgb = (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb + S.rgb * D.rgb;
    } else if (mode == 25) {
      vec4 dstSrcAlpha = D * S.a;
      result.rgb = blendSetLuminance(blendSetSaturation(S.rgb * D.a, dstSrcAlpha.rgb), dstSrcAlpha.a, dstSrcAlpha.rgb);
      result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
    } else if (mode == 26) {
      vec4 dstSrcAlpha = D * S.a;
      result.rgb = blendSetLuminance(blendSetSaturation(dstSrcAlpha.rgb, S.rgb * D.a), dstSrcAlpha.a, dstSrcAlpha.rgb);
      result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
    } else if (mode == 27) {
      vec4 srcDstAlpha = S * D.a;
      result.rgb = blendSetLuminance(srcDstAlpha.rgb, srcDstAlpha.a, D.rgb * S.a);
      result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
    } else if (mode == 28) {
      vec4 srcDstAlpha = S * D.a;
      result.rgb = blendSetLuminance(D.rgb * S.a, srcDstAlpha.a, srcDstAlpha.rgb);
      result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
    } else {
      result.rgb = clamp(1.0 + S.rgb + D.rgb - D.a - S.a, 0.0, 1.0);
      result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;
    }
  }

  return result;
}

void main() {
  vec4 inputColor = Color;

  // Sample child[0] based on CHILD0_MODE.
#if CHILD0_MODE == 0
  vec4 childColor = texture(TextureSampler_0, TransformedCoords_0);
#elif CHILD0_MODE == 1
  // ConstColorProcessor: output is just the uniform color modulated by input alpha.
  vec4 childColor = ChildConstColor * inputColor.a;
#elif CHILD0_MODE == 2
  // TiledTextureEffect: apply per-axis tiling to coordinates before sampling.
  vec2 tiledCoord = TransformedCoords_0;
  tiledCoord.x = applyTileMode(tiledCoord.x, TileModeX, TiledSubset.x, TiledSubset.z, TiledClamp.x, TiledClamp.z);
  tiledCoord.y = applyTileMode(tiledCoord.y, TileModeY, TiledSubset.y, TiledSubset.w, TiledClamp.y, TiledClamp.w);
  vec4 childColor = texture(TextureSampler_0, tiledCoord);
  // ClampToBorderNearest (mode 7): set to transparent if outside subset.
  if (TileModeX == 7 && (TransformedCoords_0.x < TiledSubset.x || TransformedCoords_0.x >= TiledSubset.z)) {
    childColor = vec4(0.0);
  }
  if (TileModeY == 7 && (TransformedCoords_0.y < TiledSubset.y || TransformedCoords_0.y >= TiledSubset.w)) {
    childColor = vec4(0.0);
  }
  // ClampToBorderLinear (mode 8): fade at edges.
  if (TileModeX == 8) {
    float fadeX = smoothstep(TiledSubset.x - 0.5, TiledSubset.x + 0.5, TransformedCoords_0.x) *
                  (1.0 - smoothstep(TiledSubset.z - 0.5, TiledSubset.z + 0.5, TransformedCoords_0.x));
    childColor *= fadeX;
  }
  if (TileModeY == 8) {
    float fadeY = smoothstep(TiledSubset.y - 0.5, TiledSubset.y + 0.5, TransformedCoords_0.y) *
                  (1.0 - smoothstep(TiledSubset.w - 0.5, TiledSubset.w + 0.5, TransformedCoords_0.y));
    childColor *= fadeY;
  }
#endif

  vec4 srcColor;
  vec4 dstColor;

#if CHILD_TYPE == 0
  // DstChild: input is src, child[0] is dst.
  srcColor = inputColor;
  dstColor = childColor;
#elif CHILD_TYPE == 1
  // SrcChild: child[0] is src, input is dst.
  srcColor = childColor;
  dstColor = inputColor;
#elif CHILD_TYPE == 2
  // TwoChild: child[0] is src, child[1] (TextureSampler_1) is dst.
  srcColor = childColor;
  dstColor = texture(TextureSampler_1, TransformedCoords_0);
#endif

#define TGFX_XP_SRC_COLOR blendColors(srcColor, dstColor)
#include "xp_output.inc"
}
