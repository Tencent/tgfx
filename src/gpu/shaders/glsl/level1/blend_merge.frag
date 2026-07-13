// BlendMergeShader fragment shader
// Permutation dimensions (frag):
//   BLEND_MODE (0~29): BlendMode enum (Clear=0, Src=1, ..., PlusDarker=29)
//   CHILD_TYPE (0~2): 0=DstChild, 1=SrcChild, 2=TwoChild
#version 450

#ifndef BLEND_MODE
#define BLEND_MODE 3
#endif
#ifndef CHILD_TYPE
#define CHILD_TYPE 0
#endif
#ifndef HAS_XP
#define HAS_XP 0
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
#if CHILD_TYPE == 2
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
#define XP_DST_TEX_BINDING 2
#else
#define XP_DST_TEX_BINDING 1
#endif
#include "xp_porter_duff.inc"

layout(location = 0) out vec4 fragColor;

// --- Blend helper functions ---

#if BLEND_MODE >= 25
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
#endif

vec4 blendColors(vec4 S, vec4 D) {
  vec4 result;

#if BLEND_MODE == 1
  // Src
  result = S;
#elif BLEND_MODE == 2
  // Dst
  result = D;
#elif BLEND_MODE == 3
  // SrcOver
  result = S + D * (1.0 - S.a);
#elif BLEND_MODE == 4
  // DstOver
  result = S * (1.0 - D.a) + D;
#elif BLEND_MODE == 5
  // SrcIn
  result = S * D.a;
#elif BLEND_MODE == 6
  // DstIn
  result = D * S.a;
#elif BLEND_MODE == 7
  // SrcOut
  result = S * (1.0 - D.a);
#elif BLEND_MODE == 8
  // DstOut
  result = D * (1.0 - S.a);
#elif BLEND_MODE == 9
  // SrcATop
  result = S * D.a + D * (1.0 - S.a);
#elif BLEND_MODE == 10
  // DstATop
  result = S * (1.0 - D.a) + D * S.a;
#elif BLEND_MODE == 11
  // Xor
  result = S * (1.0 - D.a) + D * (1.0 - S.a);
#elif BLEND_MODE == 12
  // PlusLighter
  result = clamp(S + D, 0.0, 1.0);
#elif BLEND_MODE == 13
  // Modulate
  result = S * D;
#elif BLEND_MODE == 14
  // Screen
  result = S + D - S * D;
#elif BLEND_MODE >= 15
  // Advanced blend modes share alpha formula: Sa + (1 - Sa) * Da
  result.a = S.a + (1.0 - S.a) * D.a;

#if BLEND_MODE == 15
  // Overlay (= HardLight with src/dst swapped)
  result.r = (2.0 * D.r < D.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (S.a - S.r) * (D.a - D.r);
  result.g = (2.0 * D.g < D.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (S.a - S.g) * (D.a - D.g);
  result.b = (2.0 * D.b < D.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (S.a - S.b) * (D.a - D.b);
  result.rgb += D.rgb * (1.0 - S.a) + S.rgb * (1.0 - D.a);
#elif BLEND_MODE == 16
  // Darken
  result.rgb = min((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
#elif BLEND_MODE == 17
  // Lighten
  result.rgb = max((1.0 - S.a) * D.rgb + S.rgb, (1.0 - D.a) * S.rgb + D.rgb);
#elif BLEND_MODE == 18
  // ColorDodge
  {
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
  }
#elif BLEND_MODE == 19
  // ColorBurn
  {
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
  }
#elif BLEND_MODE == 20
  // HardLight
  result.r = (2.0 * S.r < S.a) ? 2.0 * S.r * D.r : S.a * D.a - 2.0 * (D.a - D.r) * (S.a - S.r);
  result.g = (2.0 * S.g < S.a) ? 2.0 * S.g * D.g : S.a * D.a - 2.0 * (D.a - D.g) * (S.a - S.g);
  result.b = (2.0 * S.b < S.a) ? 2.0 * S.b * D.b : S.a * D.a - 2.0 * (D.a - D.b) * (S.a - S.b);
  result.rgb += S.rgb * (1.0 - D.a) + D.rgb * (1.0 - S.a);
#elif BLEND_MODE == 21
  // SoftLight
  if (D.a == 0.0) {
    result = S;
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
#elif BLEND_MODE == 22
  // Difference
  result.rgb = S.rgb + D.rgb - 2.0 * min(S.rgb * D.a, D.rgb * S.a);
#elif BLEND_MODE == 23
  // Exclusion
  result.rgb = D.rgb + S.rgb - 2.0 * D.rgb * S.rgb;
#elif BLEND_MODE == 24
  // Multiply
  result.rgb = (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb + S.rgb * D.rgb;
#elif BLEND_MODE == 25
  // Hue: SetLum(SetSat(S*Da, Sat(D*Sa)), Sa*Da, D*Sa)
  {
    vec4 dstSrcAlpha = D * S.a;
    result.rgb = blendSetLuminance(blendSetSaturation(S.rgb * D.a, dstSrcAlpha.rgb), dstSrcAlpha.a, dstSrcAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  }
#elif BLEND_MODE == 26
  // Saturation: SetLum(SetSat(D*Sa, Sat(S*Da)), Sa*Da, D*Sa)
  {
    vec4 dstSrcAlpha = D * S.a;
    result.rgb = blendSetLuminance(blendSetSaturation(dstSrcAlpha.rgb, S.rgb * D.a), dstSrcAlpha.a, dstSrcAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  }
#elif BLEND_MODE == 27
  // Color: SetLum(S*Da, Sa*Da, D*Sa)
  {
    vec4 srcDstAlpha = S * D.a;
    result.rgb = blendSetLuminance(srcDstAlpha.rgb, srcDstAlpha.a, D.rgb * S.a);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  }
#elif BLEND_MODE == 28
  // Luminosity: SetLum(D*Sa, Sa*Da, S*Da)
  {
    vec4 srcDstAlpha = S * D.a;
    result.rgb = blendSetLuminance(D.rgb * S.a, srcDstAlpha.a, srcDstAlpha.rgb);
    result.rgb += (1.0 - S.a) * D.rgb + (1.0 - D.a) * S.rgb;
  }
#elif BLEND_MODE == 29
  // PlusDarker
  result.rgb = clamp(1.0 + S.rgb + D.rgb - D.a - S.a, 0.0, 1.0);
  result.rgb *= (result.a > 0.0) ? 1.0 : 0.0;
#endif

#endif

  return result;
}

void main() {
  vec4 inputColor = Color;
  vec4 childColor = texture(TextureSampler_0, TransformedCoords_0);

  vec4 srcColor;
  vec4 dstColor;

#if CHILD_TYPE == 0
  // DstChild: input is src, child texture is dst
  srcColor = inputColor;
  dstColor = childColor;
#elif CHILD_TYPE == 1
  // SrcChild: child texture is src, input is dst
  srcColor = childColor;
  dstColor = inputColor;
#elif CHILD_TYPE == 2
  // TwoChild: first texture is src, second texture is dst
  srcColor = childColor;
  dstColor = texture(TextureSampler_1, TransformedCoords_0);
#endif

#define TGFX_XP_SRC_COLOR blendColors(srcColor, dstColor)
#include "xp_output.inc"
}
