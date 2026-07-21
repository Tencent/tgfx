// BlendMergeShader fragment shader
// Permutation dimensions (frag):
//   CHILD_TYPE (0~2): 0=DstChild, 1=SrcChild, 2=TwoChild
//   HAS_XP (0~2): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF
//   CHILD0_MODE (0~1): 0=TextureEffect (sample from TextureSampler_0),
//                       1=ConstColor (use ChildConstColor uniform, no texture)
//   Subset clamping is always applied for plain TextureEffect children via the Child0Subset /
//   Child1Subset uniforms (populated by XfermodeFragmentProcessor::onSetData through
//   computeSubsetRect). When a source has no real subset the bounds are the full [0,1] range, so
//   the clamp degenerates to a no-op. Two separate uniforms are used (instead of a shared "Subset")
//   because the precompiled path strips uniform name suffixes, so the two texture children would
//   otherwise collide on a single field.
//   HAS_COVERAGE (0/1): whether per-vertex AA coverage is provided via vCoverage varying.
//   HAS_COLOR (0/1): whether per-vertex color is provided via vColor varying (replaces Color
//                     uniform).
//
// BLEND_MODE is a runtime uniform (not a compile-time permutation dimension).
//
// When CHILD0_MODE=1 (ConstColor), the first child processor is a ConstColorProcessor that
// outputs a solid uniform color instead of sampling a texture. This is common for
// ModeColorFilter (blend a solid color with the input) and DropShadow effects.
//
// TiledTextureEffect children are not supported by this precompiled shader; the matcher rejects
// them so they fall back to ProgramBuilder.
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
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
#if !HAS_COLOR
  vec4 Color;
#endif
  int BlendModeValue;
#if CHILD0_MODE == 1
  vec4 ConstColor;
#endif
#if CHILD0_MODE == 0
  // Normalized subset bounds for child[0]'s TextureEffect (populated by
  // XfermodeFragmentProcessor::onSetData via computeSubsetRect). Layout: {left, top, right, bottom}.
  // Always present for a plain TextureEffect child; when the source has no real subset the bounds
  // are the full [0,1] range, making the clamp below a no-op.
  vec4 Child0Subset;
#endif
#if CHILD_TYPE == 2
  // Normalized subset bounds for child[1]'s TextureEffect in TwoChild mode (full [0,1] when the
  // source has no real subset).
  vec4 Child1Subset;
#endif
  vec4 Rect;
  int HasClip;
#if HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
#endif
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;

#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

#if HAS_COLOR
layout(location = 2) in vec4 vColor;
#endif

#if CHILD0_MODE == 0
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
  #if CHILD_TYPE == 2
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
    #define NEXT_BINDING 2
  #else
    #define NEXT_BINDING 1
  #endif
#elif CHILD0_MODE == 1
  // ConstColor child: first texture binding slot is freed.
  #if CHILD_TYPE == 2
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_1;
    #define NEXT_BINDING 1
  #else
    #define NEXT_BINDING 0
  #endif
#endif

#if HAS_MASK_TEXTURE
layout(set = 1, binding = NEXT_BINDING) uniform sampler2D MaskTextureSampler;
  #define XP_DST_TEX_BINDING (NEXT_BINDING + 1)
#else
  #define XP_DST_TEX_BINDING NEXT_BINDING
#endif
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

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
#if HAS_COLOR
  vec4 inputColor = vColor;
#else
  vec4 inputColor = Color;
#endif

  // Sample child[0] based on CHILD0_MODE.
#if CHILD0_MODE == 0
  vec2 coord0 = TransformedCoords_0;
  // Clamp to child[0]'s subset bounds to prevent sampling outside the valid texel region (e.g. when
  // drawing a sub-rect of an atlas or a non-power-of-two image). Full [0,1] bounds make this a
  // no-op for plain textures.
  coord0 = clamp(coord0, Child0Subset.xy, Child0Subset.zw);
  vec4 childColor = texture(TextureSampler_0, coord0);
#elif CHILD0_MODE == 1
  // ConstColorProcessor: output is just the uniform color modulated by input alpha.
  vec4 childColor = ConstColor * inputColor.a;
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
  vec2 coord1 = TransformedCoords_0;
  // Clamp to child[1]'s subset bounds (same rationale as child[0]; full [0,1] = no-op).
  coord1 = clamp(coord1, Child1Subset.xy, Child1Subset.zw);
  dstColor = texture(TextureSampler_1, coord1);
#endif

  vec4 blendResult = blendColors(srcColor, dstColor);

#if HAS_MASK_TEXTURE
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  float maskAlpha = texture(MaskTextureSampler, maskCoord.xy).r;
  blendResult *= maskAlpha;
#endif

  if (HasClip == 1) {
    highp vec4 clipDists = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - Rect), 0.0, 1.0);
    highp vec2 clipDists2 = clipDists.xy + clipDists.zw - 1.0;
    highp float clipCoverage = clipDists2.x * clipDists2.y;
    blendResult *= clipCoverage;
  }

#define TGFX_XP_SRC_COLOR blendResult
#if HAS_COVERAGE
#define TGFX_XP_COVERAGE vec4(vCoverage)
#endif
#include "xp_output.inc"
}
