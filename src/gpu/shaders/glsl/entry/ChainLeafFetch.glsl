// The leaf fetch helpers, shared by the interpreter's assembly, the composed text's prefetch and
// the Vulkan op module: the plain fetch's sample position (perspective divide + device-space
// switch), the clamped sample with the RGBAAA second-area read, and the tiled path's
// coordinate-varying picker.
#ifndef TGSL_CHAIN_LEAF_FETCH_INCLUDED
#define TGSL_CHAIN_LEAF_FETCH_INCLUDED

// The leaf's sample position before any subset or tiling math. Perspective divide: affine
// transforms produce z=1.0, making this a no-op (same contract as the QuadTexture family); the
// tiled/subset math then runs on the divided coordinate, matching the runtime's
// emitPerspTextCoord ordering. A device-space leaf samples at the fragment's destination position,
// like the runtime DeviceSpaceTextureEffect; its Subset is unbounded, so the plain clamp is a no-op
// for it. This form reads the interpreter's named block directly — the tiled path is the last
// consumer that does.
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

// The coordinate varying of leaf leafIndex, picked with compile-time branches so the varyings stay
// exactly what the vertex stage and the reflection expect. Interpreter-only (the tiled path).
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

// The plain leaf fetch: clamps to the leaf's Subset rect (full bounds = no-op), then reads through
// the leaf's own sampler (the file-scope leaf bindings every consumer declares). An RGBAAA leaf
// (always a plain one) reads its alpha from the second area at leafAlphaStart past the clamped
// coordinate, as GLSLTextureEffect::emitDefaultTextureCode does. Every fact arrives as an explicit
// argument — one spelling shared by the interpreter's variant text, the GL assembly and the Vulkan
// body — with the quad subset as a runtime flag so one form serves strict and plain draws alike.
vec4 chainLeafFetch(vec3 coord, vec4 leafSubset, vec2 leafAlphaStart, int leafHasRgbaaa,
                    int leafClampOn, int leafIndex, int deviceLeafMask, mat3 leafMatrix,
                    int readSwizzle, vec2 fragCoord, int applyQuadSubset, vec4 quadSubset) {
  highp vec2 uv = coord.xy / coord.z;
  if (((deviceLeafMask >> uint(leafIndex)) & 1) != 0) {
    highp vec3 deviceCoord = leafMatrix * vec3(fragCoord, 1.0);
    uv = deviceCoord.xy;
  }
  if (applyQuadSubset != 0) {
    // A strict draw's per-quad subset clamps leaf 0 ahead of its Subset uniform, the order of the
    // runtime TextureEffect's extra subset.
    uv = aotTextureClamp(uv, quadSubset, true);
  }
  // Gated like the fill pipeline's Subset clamp: a leaf the binder's subset rule does not clamp
  // (wrap-tiled, device-space) must read its sampler unclamped.
  highp vec2 finalCoord = aotTextureClamp(uv, leafSubset, leafClampOn != 0);
  vec4 color;
  if (leafIndex == 0) {
    color = aotTextureRead(TextureSampler_0, finalCoord, readSwizzle);
  } else if (leafIndex == 1) {
    color = aotTextureRead(TextureSampler_1, finalCoord, readSwizzle);
  } else if (leafIndex == 2) {
    color = aotTextureRead(TextureSampler_2, finalCoord, readSwizzle);
  } else {
    color = aotTextureRead(TextureSampler_3, finalCoord, readSwizzle);
  }
  if (leafHasRgbaaa != 0) {
    vec4 alpha;
    if (leafIndex == 0) {
      alpha = aotTextureRead(TextureSampler_0, finalCoord + leafAlphaStart, readSwizzle);
    } else if (leafIndex == 1) {
      alpha = aotTextureRead(TextureSampler_1, finalCoord + leafAlphaStart, readSwizzle);
    } else if (leafIndex == 2) {
      alpha = aotTextureRead(TextureSampler_2, finalCoord + leafAlphaStart, readSwizzle);
    } else {
      alpha = aotTextureRead(TextureSampler_3, finalCoord + leafAlphaStart, readSwizzle);
    }
    color = aotTextureDecodeRGBAAA(color, alpha);
  }
  return color;
}

#endif  // TGSL_CHAIN_LEAF_FETCH_INCLUDED
