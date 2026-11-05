// Fragment stage of the pointwise kernels: shared core, part 3 of 3, leaf fetch, slot evaluation
// and main(). The entry defines chainLoadTiledRecipe() (through its tiling module) and
// evalResourceSlot() before including this file.

#if NTEX > 0
// The leaf's sample position before any subset or tiling math. Perspective divide: affine
// transforms produce z=1.0, making this a no-op (same contract as QuadTextureFillShader); the
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
vec4 chainPlainLeafFetch(CHAIN_LEAF_SAMPLER texSampler, vec3 coord, vec4 leafSubset,
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
  // Gated like the fill pipeline's Subset clamp: a leaf the runtime needSubset() does not clamp
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

// Tiled leaf fetch: the shared tiling math, so wrap and clamp-to-border modes match the runtime.
// This is the heavy part of a leaf fetch (coordinate mapping, a seam blend with up to three extra
// reads, the border post-pass). main() calls it from a single site inside a loop over the leaves,
// reading the leaf's texture through aotChainRead's per-index sampler pick, so the tiling code
// exists once in the binary instead of once per leaf (a GPU compiler inlines every call site).
vec4 chainTiledLeafFetch(int leafIndex) {
  highp vec2 uv = chainLeafCoord(chainLeafCoordVarying(leafIndex), leafIndex);
  int readSwizzle = LeafReadSwizzles[leafIndex];
  bool strict = false;
  chainLoadTiledRecipe(leafIndex, strict);
  vec2 inCoord = vec2(0.0);
  vec2 subsetCoord = vec2(0.0);
  vec2 clampedCoord = vec2(0.0);
  vec2 sampleCoord = tiledMapCoord(uv, strict, inCoord, subsetCoord, clampedCoord);
  vec4 texColor = aotChainRead(leafIndex, sampleCoord, readSwizzle);
  texColor = tiledSeamBlend(leafIndex, texColor, subsetCoord, clampedCoord, readSwizzle);
  return tiledApplyBorder(texColor, inCoord, subsetCoord, clampedCoord);
}

// Whether leaf leafIndex goes through the shader-side tiling: some recipe set names it. Reads the
// recipe selectors only (the constant-bound loop unrolls into two or three compares), so the plain
// fetches in main() do not load any tiling state.
bool chainLeafIsTiled(int leafIndex) {
  for (int recipe = 0; recipe < CHAIN_TILED_RECIPES; ++recipe) {
    if (chainTiledRecipeLeaf(recipe) == leafIndex) {
      return true;
    }
  }
  return false;
}
#endif

layout(location = 0) out vec4 fragColor;

vec4 chainResults[16];

// Prefetched leaf samples. Fetched in main (the samplers stay statically bound, see aotChainRead);
// the OP_TEXTURE branch of evalChainSlot reads them back by instruction index.
vec4 chainLeafTex[4];

// The per-pixel GP edge coverage of the ellipse (GP_LAYOUT=1), hairline-line (GP_LAYOUT=2) and
// complex rounded-rect (GP_LAYOUT=3/4) layouts. Computed ahead of the chain evaluation in main so
// the coverage subtree's unit input can consume it like the rect layouts' vCoverage varying; 1.0
// keeps the define valid for every other variant.
highp float gpPixelCoverage = 1.0;

// Geometry color source: the per-vertex color attribute, or the exact Color uniform for
// common-color draws (the broadcast into the unconditional attribute is 8-bit quantized). The
// selection is a runtime uniform, so a single variant serves both sources.
#define TGFX_CHAIN_GEOM_COLOR ((HasCommonColor != 0) ? Color : vColor)

// The coverage subtree's unit input (slot designator -3): the GP's output coverage, which is
// what the runtime coverage chain starts from. The rect layouts carry it as the vCoverage
// varying; the ellipse and hairline layouts evaluate their per-pixel edge coverage ahead of the
// chain (see gpPixelCoverage). The vec4(1.0) form keeps the define valid for the remaining
// variants.
#if GP_LAYOUT == 0 && HAS_COVERAGE
#define TGFX_CHAIN_UNIT_COVERAGE vec4(vCoverage)
#elif GP_LAYOUT >= 1
#define TGFX_CHAIN_UNIT_COVERAGE vec4(gpPixelCoverage)
#else
#define TGFX_CHAIN_UNIT_COVERAGE vec4(1.0)
#endif
// Structural accessors (specialization region). The generic bodies read the uniform block, so an
// unspecialized compile is behaviorally identical to reading the members directly (the compiler
// inlines them away). The runtime specialization pass replaces this whole region between the
// tgfx pragmas with folded constants — every read site below goes through these accessors only.
#pragma tgfx_spec_accessors begin
#ifdef TGFX_SPEC_CONSTANTS
// WebGPU path: tint's SPIR-V reader inlines every function and renames all identifiers, so the
// text-rewrite specialization cannot anchor on function names in WGSL. This profile instead
// uses the platform's native mechanism: specialization constants survive the translation as
// WGSL override constants, and the browser folds them at pipeline creation. CHAIN_SPECIALIZED
// is the master switch: 0 (the default) keeps the generic tier-0 behavior reading the uniform
// block; the runtime injects 1 plus the structural values per chain shape.
//
// Two tint reader constraints shape this code:
// - Any expression a spec constant takes part in at compile time becomes OpSpecConstantOp,
//   which tint's reader rejects outright (crbug.com/tint/111). So every comparison routes
//   through a plain local variable first: the store is a normal instruction, the comparison a
//   runtime IEqual — and after the browser injects the pipeline constants it propagates and
//   folds all the same.
// - Spec-constant composites (an ivec4 built from spec ids) are equally unsupported, so the
//   slot words are assembled from four scalar locals.
layout(constant_id = 0) const int CHAIN_SPECIALIZED = 0;
layout(constant_id = 1) const int SC_SlotCount = 0;
layout(constant_id = 2) const int SC_RootIndex = 0;
layout(constant_id = 3) const int SC_CoverageRoot = 0;
layout(constant_id = 4) const int SC_ClipReg = 0;
layout(constant_id = 10) const int SC_S0_0 = 0;
layout(constant_id = 11) const int SC_S0_1 = 0;
layout(constant_id = 12) const int SC_S0_2 = 0;
layout(constant_id = 13) const int SC_S0_3 = 0;
layout(constant_id = 14) const int SC_S1_0 = 0;
layout(constant_id = 15) const int SC_S1_1 = 0;
layout(constant_id = 16) const int SC_S1_2 = 0;
layout(constant_id = 17) const int SC_S1_3 = 0;
layout(constant_id = 18) const int SC_S2_0 = 0;
layout(constant_id = 19) const int SC_S2_1 = 0;
layout(constant_id = 20) const int SC_S2_2 = 0;
layout(constant_id = 21) const int SC_S2_3 = 0;
layout(constant_id = 22) const int SC_S3_0 = 0;
layout(constant_id = 23) const int SC_S3_1 = 0;
layout(constant_id = 24) const int SC_S3_2 = 0;
layout(constant_id = 25) const int SC_S3_3 = 0;
layout(constant_id = 26) const int SC_S4_0 = 0;
layout(constant_id = 27) const int SC_S4_1 = 0;
layout(constant_id = 28) const int SC_S4_2 = 0;
layout(constant_id = 29) const int SC_S4_3 = 0;
layout(constant_id = 30) const int SC_S5_0 = 0;
layout(constant_id = 31) const int SC_S5_1 = 0;
layout(constant_id = 32) const int SC_S5_2 = 0;
layout(constant_id = 33) const int SC_S5_3 = 0;
layout(constant_id = 34) const int SC_S6_0 = 0;
layout(constant_id = 35) const int SC_S6_1 = 0;
layout(constant_id = 36) const int SC_S6_2 = 0;
layout(constant_id = 37) const int SC_S6_3 = 0;
layout(constant_id = 38) const int SC_S7_0 = 0;
layout(constant_id = 39) const int SC_S7_1 = 0;
layout(constant_id = 40) const int SC_S7_2 = 0;
layout(constant_id = 41) const int SC_S7_3 = 0;
layout(constant_id = 42) const int SC_S8_0 = 0;
layout(constant_id = 43) const int SC_S8_1 = 0;
layout(constant_id = 44) const int SC_S8_2 = 0;
layout(constant_id = 45) const int SC_S8_3 = 0;
layout(constant_id = 46) const int SC_S9_0 = 0;
layout(constant_id = 47) const int SC_S9_1 = 0;
layout(constant_id = 48) const int SC_S9_2 = 0;
layout(constant_id = 49) const int SC_S9_3 = 0;
layout(constant_id = 50) const int SC_S10_0 = 0;
layout(constant_id = 51) const int SC_S10_1 = 0;
layout(constant_id = 52) const int SC_S10_2 = 0;
layout(constant_id = 53) const int SC_S10_3 = 0;
layout(constant_id = 54) const int SC_S11_0 = 0;
layout(constant_id = 55) const int SC_S11_1 = 0;
layout(constant_id = 56) const int SC_S11_2 = 0;
layout(constant_id = 57) const int SC_S11_3 = 0;
layout(constant_id = 58) const int SC_S12_0 = 0;
layout(constant_id = 59) const int SC_S12_1 = 0;
layout(constant_id = 60) const int SC_S12_2 = 0;
layout(constant_id = 61) const int SC_S12_3 = 0;
layout(constant_id = 62) const int SC_S13_0 = 0;
layout(constant_id = 63) const int SC_S13_1 = 0;
layout(constant_id = 64) const int SC_S13_2 = 0;
layout(constant_id = 65) const int SC_S13_3 = 0;
layout(constant_id = 66) const int SC_S14_0 = 0;
layout(constant_id = 67) const int SC_S14_1 = 0;
layout(constant_id = 68) const int SC_S14_2 = 0;
layout(constant_id = 69) const int SC_S14_3 = 0;
layout(constant_id = 70) const int SC_S15_0 = 0;
layout(constant_id = 71) const int SC_S15_1 = 0;
layout(constant_id = 72) const int SC_S15_2 = 0;
layout(constant_id = 73) const int SC_S15_3 = 0;
int chainSlotCount() {
  int spec = CHAIN_SPECIALIZED;
  if (spec == 1) { return SC_SlotCount; }
  return SlotCount;
}
int chainRootIndex() {
  int spec = CHAIN_SPECIALIZED;
  if (spec == 1) { return SC_RootIndex; }
  return RootIndex;
}
int chainCoverageRootIndex() {
  int spec = CHAIN_SPECIALIZED;
  if (spec == 1) { return SC_CoverageRoot; }
  return CoverageRootIndex;
}
int chainClipRegister() {
  int spec = CHAIN_SPECIALIZED;
  if (spec == 1) { return SC_ClipReg; }
  return ClipCoverageRegister;
}
ivec4 chainSlot(int slot) {
  int spec = CHAIN_SPECIALIZED;
  if (spec == 1) {
    if (slot == 0) {
      int a = SC_S0_0; int b = SC_S0_1; int c = SC_S0_2; int d = SC_S0_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 1) {
      int a = SC_S1_0; int b = SC_S1_1; int c = SC_S1_2; int d = SC_S1_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 2) {
      int a = SC_S2_0; int b = SC_S2_1; int c = SC_S2_2; int d = SC_S2_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 3) {
      int a = SC_S3_0; int b = SC_S3_1; int c = SC_S3_2; int d = SC_S3_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 4) {
      int a = SC_S4_0; int b = SC_S4_1; int c = SC_S4_2; int d = SC_S4_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 5) {
      int a = SC_S5_0; int b = SC_S5_1; int c = SC_S5_2; int d = SC_S5_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 6) {
      int a = SC_S6_0; int b = SC_S6_1; int c = SC_S6_2; int d = SC_S6_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 7) {
      int a = SC_S7_0; int b = SC_S7_1; int c = SC_S7_2; int d = SC_S7_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 8) {
      int a = SC_S8_0; int b = SC_S8_1; int c = SC_S8_2; int d = SC_S8_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 9) {
      int a = SC_S9_0; int b = SC_S9_1; int c = SC_S9_2; int d = SC_S9_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 10) {
      int a = SC_S10_0; int b = SC_S10_1; int c = SC_S10_2; int d = SC_S10_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 11) {
      int a = SC_S11_0; int b = SC_S11_1; int c = SC_S11_2; int d = SC_S11_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 12) {
      int a = SC_S12_0; int b = SC_S12_1; int c = SC_S12_2; int d = SC_S12_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 13) {
      int a = SC_S13_0; int b = SC_S13_1; int c = SC_S13_2; int d = SC_S13_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 14) {
      int a = SC_S14_0; int b = SC_S14_1; int c = SC_S14_2; int d = SC_S14_3;
      return ivec4(a, b, c, d);
    }
    if (slot == 15) {
      int a = SC_S15_0; int b = SC_S15_1; int c = SC_S15_2; int d = SC_S15_3;
      return ivec4(a, b, c, d);
    }
    return ivec4(4, -2, -2, 0);
  }
  return SlotPacked[slot];
}
#else
int chainSlotCount() { return SlotCount; }
ivec4 chainSlot(int slot) { return SlotPacked[slot]; }
int chainRootIndex() { return RootIndex; }
int chainCoverageRootIndex() { return CoverageRootIndex; }
int chainClipRegister() { return ClipCoverageRegister; }
#endif
#pragma tgfx_spec_accessors end
#include "pipeline/chain/pointwise_chain_eval.inc"

// The XferProcessor source without coverage: the root, times the coverage subtree root in the
// layouts without a coverage varying (see the epilogue below).
#if GP_LAYOUT >= 1 || HAS_COVERAGE
#define CHAIN_XP_SRC_UNPREMUL result
#else
#define CHAIN_XP_SRC_UNPREMUL \
  (chainCoverageRootIndex() >= 0 ? result * coverageRootRegister : result)
#endif

// Slot executor (specialization region). The generic body keeps the runtime loop — the compiler
// cannot unroll it because the bound is uniform data. The specialization pass replaces the region
// with one call site per slot (literal slot indices), which lets the op dispatch, the register
// reads and the result-register writes fold to constants without relying on driver unrolling.
#pragma tgfx_spec_executor begin
#ifdef TGFX_SPEC_CONSTANTS
// The specialization executor: one call site per slot with literal indices, guarded by the
// SC_SlotCount specialization constant. Once the runtime injects the values, every guard and
// every selector word becomes a compile-time constant, so the driver's specialization step
// deletes the out-of-range call sites entirely — the static reduction the generic loop's
// runtime trip counter can never provide.
void chainRunSlotAt(int n, out vec4 result, out vec4 clipRegister,
                    out vec4 coverageRootRegister
#if HAS_XP >= 1
                    , out vec4 xpBlended
#endif
) {
  ivec4 Packed = chainSlot(n);
  vec4 value = evalChainSlot(n);
  int outRegister = (Packed.w >> 20) & 0x1F;
  if (outRegister != 0) {
    int written = outRegister - 1;
    chainResults[written] = value;
    if (written == chainRootIndex()) {
      result = value;
    }
    if (written == chainClipRegister()) {
      clipRegister = value;
    }
    if (written == chainCoverageRootIndex()) {
      coverageRootRegister = value;
    }
  }
}
void chainRunSlots(out vec4 result, out vec4 clipRegister, out vec4 coverageRootRegister
#if HAS_XP >= 1
                   ,
                   out vec4 xpBlended
#endif
) {
  result = vec4(0.0);
  clipRegister = vec4(0.0);
  coverageRootRegister = vec4(0.0);
#if HAS_XP >= 1
  xpBlended = vec4(0.0);
#endif
    if (chainSlotCount() > 0) {
    chainRunSlotAt(0, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 1) {
    chainRunSlotAt(1, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 2) {
    chainRunSlotAt(2, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 3) {
    chainRunSlotAt(3, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 4) {
    chainRunSlotAt(4, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 5) {
    chainRunSlotAt(5, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 6) {
    chainRunSlotAt(6, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 7) {
    chainRunSlotAt(7, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 8) {
    chainRunSlotAt(8, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 9) {
    chainRunSlotAt(9, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 10) {
    chainRunSlotAt(10, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 11) {
    chainRunSlotAt(11, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 12) {
    chainRunSlotAt(12, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 13) {
    chainRunSlotAt(13, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 14) {
    chainRunSlotAt(14, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
    if (chainSlotCount() > 15) {
    chainRunSlotAt(15, result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
        , xpBlended
#endif
        );
  }
}
#else
void chainRunSlots(out vec4 result, out vec4 clipRegister, out vec4 coverageRootRegister
#if HAS_XP >= 1
                   ,
                   out vec4 xpBlended
#endif
) {
  result = vec4(0.0);
  clipRegister = vec4(0.0);
  coverageRootRegister = vec4(0.0);
#if HAS_XP >= 1
  xpBlended = vec4(0.0);
  // With an advanced XferProcessor mode the loop runs one pass past the instructions: slot
  // SlotCount blends the XferProcessor source with the dst at evalChainSlot's OP_BLEND site
  // (xp_blend_pass.inc). The pass count stays uniform data, so the loop is not unrolled.
  int passCount = chainSlotCount() + (xpNeedsBlendPass() ? 1 : 0);
#else
  int passCount = chainSlotCount();
#endif
  for (int i = 0; i < passCount; ++i) {
#if HAS_XP >= 1
    if (i == chainSlotCount()) {
      chainXPSrc = CHAIN_XP_SRC_UNPREMUL;
    }
#endif
    // Bits 20-24 of the selector carry the result register + 1 (0 = dead instruction, skip the
    // write). The value is evaluated before the write, so a consumer may share a register with
    // its just-released producer.
    vec4 value = evalChainSlot(i);
#if HAS_XP >= 1
    if (i == chainSlotCount()) {
      xpBlended = value;
    } else
#endif
    {
      int outRegister = (chainSlot(i).w >> 20) & 0x1F;
      if (outRegister != 0) {
        int written = outRegister - 1;
        chainResults[written] = value;
        if (written == chainRootIndex()) {
          result = value;
        }
        if (written == chainClipRegister()) {
          clipRegister = value;
        }
        if (written == chainCoverageRootIndex()) {
          coverageRootRegister = value;
        }
      }
    }
  }
}
#endif
#pragma tgfx_spec_executor end

void main() {
#if GP_LAYOUT == 1
  // The ellipse edge coverage depends only on varyings, so it is evaluated ahead of the chain:
  // a coverage subtree's unit input consumes it (the -3 designator), and the tail below folds it
  // into the final coverage exactly once (the subtree already carries it, so the coverage-root
  // path must not multiply it in again).
  gpPixelCoverage = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);
#elif GP_LAYOUT == 2
  // The hairline edge coverage (Coverage * edge alpha), with the same contract as the ellipse.
  gpPixelCoverage = hairlineLineEdgeCoverage(vEdgeDistance, Coverage, AAEnabled);
#elif GP_LAYOUT == 3
  // The complex rounded-rect fill coverage (per-corner radii), same contract as the ellipse.
  gpPixelCoverage =
      complexEllipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, vEdgeDist, vec2(0.0), false);
#elif GP_LAYOUT == 4
  // The complex rounded-rect stroke coverage: the outer curve times the inner one.
  gpPixelCoverage =
      complexEllipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, vEdgeDist, vStrokeWidth, true);
#endif
  // Active slots are contiguous from 0 by construction (texture leaves first, then the rest in
  // topological order), so a uniform guard skips the whole evaluation of unused slots —
  // including the Packed read — instead of relying on the OP_NONE early-out alone.
  // Texture leaves occupy slots 0..NTEX-1. Each prefetch is guarded by the slot's op marker so
  // the phantom slot of a 3-leaf chain and the sampler-only LUT child (both non-texture slots
  // occupying leaf positions) are never sampled; the loop below still evaluates them like any
  // other slot. Plain leaves are fetched one by one with their own samplers; tiled leaves (one per
  // recipe set, two or three per chain, see the tiling module) are fetched by one loop over the
  // recipe sets with a single call site, so the tiling code is not repeated per leaf. The loop
  // bound involves SlotCount so the compiler keeps it a loop (a constant bound would unroll it
  // back into one copy per recipe); every tiled leaf has its own slot, so SlotCount never cuts a
  // used recipe set off. The leaf index, the loop bound and the tiled test are uniform across the
  // draw, so every texture read stays in uniform control flow.
#if NTEX > 0
  if (chainSlot(0).x == OP_TEXTURE && !chainLeafIsTiled(0)) {
    chainLeafTex[0] = chainPlainLeafFetch(TextureSampler_0, TransformedCoords_0, Subset,
                                          AlphaStart, HasRgbaaa, SubsetClampOn, 0);
  }
  if (chainSlot(1).x == OP_TEXTURE && !chainLeafIsTiled(1)) {
    chainLeafTex[1] = chainPlainLeafFetch(TextureSampler_1, TransformedCoords_1, Subset_1,
                                          AlphaStart_1, HasRgbaaa_1, SubsetClampOn_1, 1);
  }
  if (chainSlot(2).x == OP_TEXTURE && !chainLeafIsTiled(2)) {
    chainLeafTex[2] = chainPlainLeafFetch(TextureSampler_2, TransformedCoords_2, Subset_2,
                                          AlphaStart_2, HasRgbaaa_2, SubsetClampOn_2, 2);
  }
  if (chainSlot(3).x == OP_TEXTURE && !chainLeafIsTiled(3)) {
    chainLeafTex[3] = chainPlainLeafFetch(TextureSampler_3, TransformedCoords_3, Subset_3,
                                          AlphaStart_3, HasRgbaaa_3, SubsetClampOn_3, 3);
  }
  int recipeCount = min(chainSlotCount(), CHAIN_TILED_RECIPES);
  for (int recipe = 0; recipe < recipeCount; ++recipe) {
    int leaf = chainTiledRecipeLeaf(recipe);
    if (leaf >= 0 && leaf < NTEX && chainSlot(leaf).x == OP_TEXTURE) {
      chainLeafTex[leaf] = chainTiledLeafFetch(leaf);
    }
  }
#endif
  // The three registers the epilogue consumes (root, clip coverage, coverage subtree root) are
  // captured as chainRunSlots writes them instead of being read back as
  // chainResults[<uniform>] after the loop. Registers are reused, so "the last write to that
  // register" is exactly what the read-back returned. The capture is required, not cosmetic: on
  // the Apple desktop GL driver, the zero-coverage discard in applyPorterDuffXP
  // (xp_porter_duff.inc) killed every fragment of the draw while the coverage came from that
  // read-back, although the same expression selected the correct colour when no discard was
  // present. Capturing inside the loop avoids it.
  vec4 result;
  vec4 clipRegister;
  vec4 coverageRootRegister;
#if HAS_XP >= 1
  vec4 xpBlended;
  chainXPDst = xpReadDstColor();
#endif
  chainRunSlots(result, clipRegister, coverageRootRegister
#if HAS_XP >= 1
                ,
                xpBlended
#endif
  );

  // The narrow clip slots chain from the scalar unit one (-4) instead of multiplying the color
  // root, so their product is the pure clip coverage (see ClipCoverageRegister above). The atlas
  // path routes its glyph-mask subtree root through the same register: the glyph mask is TRUE
  // coverage in the runtime composite (the dedicated MaskFill shader carries it as
  // TGFX_XP_COVERAGE), unlike a MaskFilter's source-modulating mask, whose subtree keeps the
  // CoverageRootIndex source route (MaskCoverageBlendModesOnOpaqueBackground, byte-parity across
  // Src/SrcOver/Multiply/Darken).
  vec4 clipCoverage = chainClipRegister() >= 0 ? clipRegister : vec4(1.0);

  // The device-space mask is draw COVERAGE (a clip/mask on the whole draw), so it multiplies the
  // coverage chain like the runtime's DeviceSpaceTextureEffect compositing — never the bare color.
  // Baking it into the color instead leaves the XferProcessor's dst attenuation without the mask
  // (dst is not released where the mask fades), diverging from the runtime composite.
  highp float deviceMask = 1.0;
#if NTEX > 0
  if (HasMaskTexture != 0) {
    highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    deviceMask = texture(MaskTextureSampler, maskCoord.xy).r;
  }
#elif HAS_MASK_TEXTURE
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  deviceMask = texture(MaskTextureSampler, maskCoord.xy).r;
#endif

  // The composition rule shared with the fill pipeline (fill_fragment_main.inc): the coverage
  // factors fold left in the fixed order — the geometry's own coverage first (a coverage
  // subtree's root REPLACES it: its -3 unit input already carries the GP coverage, so the plain
  // modulation must not double it), then the device mask, then the clip product. Floating-point
  // multiplication commutes exactly but does not associate, so this left-fold order is part of
  // the contract; it matches the fill pipeline's geometry → device mask → clip order.
  // The registered exception: on the layouts whose geometry contributes no coverage, the
  // subtree's root stays a source modulation instead (CHAIN_XP_SRC_UNPREMUL above) — the
  // runtime's MaskFilter semantics modulate the source, so blend(c*S, D) — not
  // c*blend(S,D) + (1-c)*D — is the matching semantics there. The atlas glyph subtree rides
  // the clip-coverage register instead (see ClipCoverageRegister above): the glyph mask is TRUE
  // coverage in the runtime composite, unlike the MaskFilter's source-modulating mask
  // (MaskCoverageBlendModesOnOpaqueBackground, byte-parity across Src/SrcOver/Multiply/Darken).
#if GP_LAYOUT >= 1 || HAS_COVERAGE
  highp vec4 finalCoverage = chainCoverageRootIndex() >= 0 ? coverageRootRegister
                                                           : TGFX_CHAIN_UNIT_COVERAGE;
#else
  highp vec4 finalCoverage = TGFX_CHAIN_UNIT_COVERAGE;
#endif
  finalCoverage *= deviceMask;
  finalCoverage *= clipCoverage;
#define TGFX_COMPOSE_SRC_UNPREMUL CHAIN_XP_SRC_UNPREMUL
#define TGFX_COMPOSE_COVERAGE finalCoverage
#if HAS_XP >= 1
#define TGFX_COMPOSE_DST chainXPDst
#define TGFX_COMPOSE_BLENDED xpBlended
#endif
#include "coverage/compose.inc"
#undef CHAIN_XP_SRC_UNPREMUL
#include "xp/output_swizzle.inc"
}
