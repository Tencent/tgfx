// Shared TiledTextureEffect sampling logic, mirroring the runtime GLSLTiledTextureEffect codegen.
// Include this file after the FragmentUniformBlock, which must declare:
//   int ShaderModeX, ShaderModeY;  vec4 Subset;  vec4 Clamp;  vec2 Dimension;
// It does not perform the texture() lookup itself (so it needs no sampler and cross-compiles
// cleanly): call tiledMapCoord() to compute the coordinate to sample, do the texture() lookup, then
// call tiledApplyBorder() on the result. This lets both a single tiled fill and the per-tap loop of
// a blur reuse identical tiling math.
//
// Supported ShaderMode values: None(0), Clamp(1), RepeatNearestNone(2), MirrorRepeat(6),
// ClampToBorderNearest(7), ClampToBorderLinear(8). RepeatLinear/mipmap modes (3,4,5) need
// multi-sample blending not implemented here and must fall back to ProgramBuilder.

// ClampToBorder modes require unnormalized (pixel-space) coordinates: the incoming coord is divided
// by Dimension before tiling and multiplied back when sampling.
bool tiledUsesUnorm(int mode) {
  return mode == 7 || mode == 8;
}

// Clamp, RepeatNearest, MirrorRepeat and ClampToBorderLinear clamp the tiled coordinate to the Clamp
// rect; None and ClampToBorderNearest do not. Matches ShaderModeUsesClamp().
bool tiledUsesClamp(int mode) {
  return mode == 1 || mode == 2 || mode == 6 || mode == 8;
}

// Computes the pre-clamp subset coordinate for a single axis, matching the runtime subsetCoord().
float tiledSubsetCoord(float coord, float subLo, float subHi, int mode) {
  if (mode == 2) {
    // RepeatNearestNone: wrap into [subLo, subHi)
    return mod(coord - subLo, subHi - subLo) + subLo;
  }
  if (mode == 6) {
    // MirrorRepeat
    float w = subHi - subLo;
    float m = mod(coord - subLo, 2.0 * w);
    return mix(m, 2.0 * w - m, step(w, m)) + subLo;
  }
  // None, Clamp, ClampToBorderNearest, ClampToBorderLinear: unchanged
  return coord;
}

// Maps an incoming texture coordinate through the tiling and returns the coordinate to sample. The
// intermediate coordinates needed by tiledApplyBorder() are returned via out parameters. When strict
// is true, the sample coordinate is additionally clamped to the Clamp rect (SrcRectConstraint).
vec2 tiledMapCoord(vec2 texCoord, bool strict, out vec2 inCoordOut, out vec2 subsetCoordOut,
                   out vec2 clampedCoordOut) {
  bool unorm = tiledUsesUnorm(ShaderModeX) || tiledUsesUnorm(ShaderModeY);
  vec2 inCoord = texCoord;
  if (unorm) {
    inCoord /= Dimension;
  }
  vec2 subsetCoord;
  subsetCoord.x = tiledSubsetCoord(inCoord.x, Subset.x, Subset.z, ShaderModeX);
  subsetCoord.y = tiledSubsetCoord(inCoord.y, Subset.y, Subset.w, ShaderModeY);
  vec2 clampedCoord;
  clampedCoord.x =
      tiledUsesClamp(ShaderModeX) ? clamp(subsetCoord.x, Clamp.x, Clamp.z) : subsetCoord.x;
  clampedCoord.y =
      tiledUsesClamp(ShaderModeY) ? clamp(subsetCoord.y, Clamp.y, Clamp.w) : subsetCoord.y;
  if (strict) {
    clampedCoord = clamp(clampedCoord, Clamp.xy, Clamp.zw);
  }
  inCoordOut = inCoord;
  subsetCoordOut = subsetCoord;
  clampedCoordOut = clampedCoord;
  return unorm ? clampedCoord * Dimension : clampedCoord;
}

// Applies the ClampToBorder post-processing to a sampled color, matching the runtime: linear border
// fades by the clamp error, nearest border discards texels whose snapped center is outside Subset.
vec4 tiledApplyBorder(vec4 color, vec2 inCoord, vec2 subsetCoord, vec2 clampedCoord) {
  if (ShaderModeX == 8) {
    float errX = subsetCoord.x - clampedCoord.x;
    color = mix(color, vec4(0.0), min(abs(errX), 1.0));
  }
  if (ShaderModeY == 8) {
    float errY = subsetCoord.y - clampedCoord.y;
    color = mix(color, vec4(0.0), min(abs(errY), 1.0));
  }
  if (ShaderModeX == 7) {
    float snappedX = floor(inCoord.x + 0.001) + 0.5;
    if (snappedX < Subset.x || snappedX > Subset.z) {
      color = vec4(0.0);
    }
  }
  if (ShaderModeY == 7) {
    float snappedY = floor(inCoord.y + 0.001) + 0.5;
    if (snappedY < Subset.y || snappedY > Subset.w) {
      color = vec4(0.0);
    }
  }
  return color;
}
