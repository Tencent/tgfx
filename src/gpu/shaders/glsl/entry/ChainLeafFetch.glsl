// The plain leaf fetch helpers, shared verbatim by the interpreter's assembly and the composed
// text's prefetch: the sample position (perspective divide + device-space switch), the clamped
// sample with the RGBAAA second-area read, and the coordinate varying pick. Packed into the
// bundle's fragment pack so the assembler can emit prefetches without the tiling machinery.
#ifndef TGSL_CHAIN_LEAF_FETCH_INCLUDED
#define TGSL_CHAIN_LEAF_FETCH_INCLUDED

// The leaf's sample position before any subset or tiling math. Perspective divide: affine
// transforms produce z=1.0, making this a no-op (same contract as the QuadTexture family); the
// tiled/subset math then runs on the divided coordinate, matching the runtime's
// emitPerspTextCoord ordering. A device-space leaf samples at the fragment's destination position,
// like the runtime DeviceSpaceTextureEffect; its Subset is unbounded, so the plain clamp is a no-op
// for it.
highp vec2 chainLeafCoord(vec3 coord, int leafIndex) {
  highp vec2 uv = coord.xy / coord.z;
  // The shift amount is unsigned: in the tiled-leaf loop leafIndex is not a constant, and WGSL
  // (through tint) only accepts a u32 shift amount.
  if (((DeviceLeafMask >> uint(leafIndex)) & 1) != 0) {
    highp vec3 deviceCoord = DeviceLeafCoordMatrix[leafIndex] * vec3(gl_FragCoord.xy, 1.0);
    uv = deviceCoord.xy;
  }
  return uv;
}

// Plain leaf fetch: clamps to the leaf's Subset rect (full bounds = no-op). An RGBAAA leaf (always
// a plain one) reads its alpha from the second area at leafAlphaStart past the clamped
// coordinate, as GLSLTextureEffect::emitDefaultTextureCode does. main() calls this once per leaf
// with the leaf's own sampler: the body is short, and statically bound per-leaf reads keep the
// common draw's sampling as direct as it can be.
vec4 chainPlainLeafFetch(sampler2D texSampler, vec3 coord, vec4 leafSubset,
                         vec2 leafAlphaStart, int leafHasRgbaaa, int leafClampOn, int leafIndex) {
  highp vec2 uv = chainLeafCoord(coord, leafIndex);
  int readSwizzle = LeafReadSwizzles[leafIndex];
#if HAS_SUBSET
  // A strict draw's per-quad subset clamps leaf 0 ahead of its Subset uniform, the order of the
  // runtime TextureEffect's extra subset (leafIndex is a constant at each call site).
  if (leafIndex == 0) {
    uv = aotTextureClamp(uv, vTexSubset, true);
  }
#endif
  // Gated like the fill pipeline's Subset clamp: a leaf the binder's subset rule does not clamp
  // (wrap-tiled, device-space) must read its sampler unclamped.
  highp vec2 finalCoord = aotTextureClamp(uv, leafSubset, leafClampOn != 0);
  vec4 color = aotTextureRead(texSampler, finalCoord, readSwizzle);
  if (leafHasRgbaaa != 0) {
    vec4 alpha = aotTextureRead(texSampler, finalCoord + leafAlphaStart, readSwizzle);
    color = aotTextureDecodeRGBAAA(color, alpha);
  }
  return color;
}

// The coordinate varying of leaf leafIndex, picked with compile-time branches so the varyings stay
// exactly what the vertex stage and the reflection expect.
vec3 chainLeafCoordVarying(int leafIndex) {
  if (leafIndex == 0) {
    return TransformedCoords_0;
  }
  if (leafIndex == 1) {
    return TransformedCoords_1;
  }
  if (leafIndex == 2) {
    return TransformedCoords_2;
  }
  return TransformedCoords_3;
}

#endif  // TGSL_CHAIN_LEAF_FETCH_INCLUDED
