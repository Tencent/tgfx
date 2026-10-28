// The full nine-mode tiled-sampling source of TiledTextureFillShader (per-axis modes 0-8):
// 0=None, 1=Clamp, 2=RepeatNearestNone, 3=RepeatLinearNone, 4=RepeatNearestMipmap,
// 5=RepeatLinearMipmap, 6=MirrorRepeat, 7=ClampToBorderNearest, 8=ClampToBorderLinear.
// The single-tap modes share tiled_sample.inc; the mipmap-repeat modes (4/5) take a 4-tap seam
// blend; RepeatLinearNone blends across the seam when the mod-wrapped coordinate clamps at a
// subset edge. Everything mirrors the runtime GLSLTiledTextureEffect codegen, including the
// Dimension normalization of the unnormalized ClampToBorder modes. AlphaOnly (alpha-only source)
// and Strict (SrcRectConstraint::Strict) are runtime uniforms rather than compile-time
// permutations: both are pure fragment math, so folding them into coherent uniform branches
// shrinks the variant count (mirrors QuadTextureFillShader). The coordinate is always affine
// (vec2); perspective TiledTextureEffect draws fall back to ProgramBuilder, so no perspective
// path exists here.
// Requires: tiled_sample.inc and texture_read_swizzle.inc already included; TextureSampler_0 and
// the recipe uniforms (ShaderModeX, ShaderModeY, Subset, Clamp, Dimension, AlphaOnly, Strict,
// ReadSwizzle) declared by the family's interface.
vec4 aotTiledRead(vec2 coord) {
  return aotReadSwizzle(texture(TextureSampler_0, coord), ReadSwizzle);
}

// Per-axis coordinate for the mipmap-repeat modes (RepeatNearestMipmap(4), RepeatLinearMipmap(5)),
// ported verbatim from GLSLTiledTextureEffect::subsetCoord. Produces the wrapped subset coord, the
// seam-neighbour coord and the blend weight for a 4-tap seam blend. For every other mode the extra
// tap collapses (extra == subset, weight == 0) so the shared 4-tap math reduces to a single tap.
void tiledMipmapAxis(float coord, float subLo, float subHi, int mode, out float subsetCoord,
                     out float extraCoord, out float weight) {
  if (mode == 4 || mode == 5) {
    highp float w = subHi - subLo;
    highp float w2 = 2.0 * w;
    highp float d = coord - subLo;
    highp float m = mod(d, w2);
    highp float o = mix(m, w2 - m, step(w, m));
    subsetCoord = o + subLo;
    extraCoord = w - o + subLo;
    highp float hw = w / 2.0;
    highp float n = mod(d - hw, w2);
    weight = clamp(mix(n, w2 - n, step(w, n)) - hw + 0.5, 0.0, 1.0);
  } else {
    subsetCoord = tiledSubsetCoord(coord, subLo, subHi, mode);
    extraCoord = subsetCoord;
    weight = 0.0;
  }
}

// The sampled, swizzled, seam-blended color, modulated by the input color: AlphaOnly broadcasts
// the red channel and multiplies the input color by it, otherwise the sample is multiplied by the
// input color's alpha.
vec4 aotTiledFullSample(vec2 texCoord, vec4 inputColor) {
  vec4 color;
  bool fourTap =
      ShaderModeX == 4 || ShaderModeX == 5 || ShaderModeY == 4 || ShaderModeY == 5;
  if (fourTap) {
    // Mipmap-repeat 4-tap seam blend (mirrors the runtime GLSLTiledTextureEffect mipmapRepeat path).
    bool unorm = tiledEffectiveUnorm(ShaderModeX, ShaderModeY);
    vec2 inC = unorm ? texCoord / Dimension : texCoord;
    float subX, extraX, weightX;
    float subY, extraY, weightY;
    tiledMipmapAxis(inC.x, Subset.x, Subset.z, ShaderModeX, subX, extraX, weightX);
    tiledMipmapAxis(inC.y, Subset.y, Subset.w, ShaderModeY, subY, extraY, weightY);
    vec2 clampedCoord =
        vec2(tiledUsesClamp(ShaderModeX) ? clamp(subX, Clamp.x, Clamp.z) : subX,
             tiledUsesClamp(ShaderModeY) ? clamp(subY, Clamp.y, Clamp.w) : subY);
    vec2 extraCoord = vec2(clamp(extraX, Clamp.x, Clamp.z), clamp(extraY, Clamp.y, Clamp.w));
    if (Strict != 0) {
      clampedCoord = clamp(clampedCoord, Clamp.xy, Clamp.zw);
      extraCoord = clamp(extraCoord, Clamp.xy, Clamp.zw);
    }
    vec2 s1 = clampedCoord;
    vec2 s2 = vec2(extraCoord.x, clampedCoord.y);
    vec2 s3 = vec2(clampedCoord.x, extraCoord.y);
    vec2 s4 = vec2(extraCoord.x, extraCoord.y);
    if (unorm) {
      s1 *= Dimension;
      s2 *= Dimension;
      s3 *= Dimension;
      s4 *= Dimension;
    }
    vec4 c1 = aotTiledRead(s1);
    vec4 c2 = aotTiledRead(s2);
    vec4 c3 = aotTiledRead(s3);
    vec4 c4 = aotTiledRead(s4);
    color = mix(mix(c1, c2, weightX), mix(c3, c4, weightX), weightY);
  } else {
    vec2 inCoord;
    vec2 subsetCoord;
    vec2 clampedCoord;
    vec2 sampleCoord = tiledMapCoord(texCoord, Strict != 0, inCoord, subsetCoord, clampedCoord);
    color = aotTiledRead(sampleCoord);
    // RepeatLinearNone seam blend: when the mod-wrapped coordinate clamps at a subset edge, blend
    // with the opposite edge so linear filtering wraps across the seam. Mirrors the runtime codegen
    // (GLSLTiledTextureEffect repeat path). Nearest repeat needs no blend. The border fade below
    // must follow the seam blend, as in the runtime codegen: a repeat axis next to a decal axis
    // blends both in the same pixels, and the two do not commute.
    bool repX = ShaderModeX == 3;
    bool repY = ShaderModeY == 3;
    if (repX || repY) {
      vec2 sampleScale = tiledEffectiveUnorm(ShaderModeX, ShaderModeY) ? Dimension : vec2(1.0);
      highp float errX = repX ? (subsetCoord.x - clampedCoord.x) : 0.0;
      highp float errY = repY ? (subsetCoord.y - clampedCoord.y) : 0.0;
      highp float repeatCoordX = errX > 0.0 ? Clamp.x : Clamp.z;
      highp float repeatCoordY = errY > 0.0 ? Clamp.y : Clamp.w;
      if (repX && repY) {
        vec4 repeatReadX =
            aotTiledRead(vec2(repeatCoordX, clampedCoord.y) * sampleScale);
        vec4 repeatReadY =
            aotTiledRead(vec2(clampedCoord.x, repeatCoordY) * sampleScale);
        vec4 repeatReadXY =
            aotTiledRead(vec2(repeatCoordX, repeatCoordY) * sampleScale);
        if (errX != 0.0 && errY != 0.0) {
          errX = abs(errX);
          color = mix(mix(color, repeatReadX, errX), mix(repeatReadY, repeatReadXY, errX),
                      abs(errY));
        } else if (errX != 0.0) {
          color = mix(color, repeatReadX, errX);
        } else if (errY != 0.0) {
          color = mix(color, repeatReadY, errY);
        }
      } else if (repX) {
        if (errX != 0.0) {
          vec4 repeatReadX =
              aotTiledRead(vec2(repeatCoordX, clampedCoord.y) * sampleScale);
          color = mix(color, repeatReadX, errX);
        }
      } else {
        if (errY != 0.0) {
          vec4 repeatReadY =
              aotTiledRead(vec2(clampedCoord.x, repeatCoordY) * sampleScale);
          color = mix(color, repeatReadY, errY);
        }
      }
    }
    color = tiledApplyBorder(color, inCoord, subsetCoord, clampedCoord);
  }

  if (AlphaOnly != 0) {
    // Alpha-only textures use R8 format in Metal. Use .r to get the actual alpha value.
    color = vec4(color.r);
    color = color.a * inputColor;
  } else {
    color = color * inputColor.a;
  }
  return color;
}
