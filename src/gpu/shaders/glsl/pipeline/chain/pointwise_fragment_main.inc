// Fragment stage of the pointwise kernels: shared core, part 3 of 3, leaf fetch, slot evaluation
// and main(). The entry defines chainLoadTiledRecipe() (through its tiling module) and
// evalResourceSlot() before including this file.

#ifndef TGSL_POINTWISE_FRAGMENT_MAIN_INCLUDED
#define TGSL_POINTWISE_FRAGMENT_MAIN_INCLUDED

#if NTEX > 0
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

vec4 chainResults[TGFX_CHAIN_REGISTERS];

// Prefetched leaf samples. Fetched in main (the samplers stay statically bound, see aotChainRead);
// the OP_TEXTURE branch of evalChainSlot reads them back by instruction index.
vec4 chainLeafTex[4];

// The per-pixel GP edge coverage of the layouts that carry one (GP_LAYOUT 1, 2, 3, 5, 7, 9:
// ellipse, hairline-line, complex rounded rect, the non-AA rounded rects and the hairline quad).
// Computed ahead of the chain evaluation in main so the coverage subtree's unit input can consume
// it like the rect layouts' vCoverage varying; 1.0 keeps the define valid for the rect variants.
highp float gpPixelCoverage = 1.0;

// The declared geometries spell their varyings with the module prefix; the body reads the bare
// names. One alias block per geometry keeps this text geometry-agnostic: the selectors are the
// variant's geometry index, and each block answers only for the names its geometry declares.
#if CHAIN_GEOMETRY == 3
#define vColor EllipseGeometry_vColor
#define vOffsets EllipseGeometry_vOffsets
#define vRadii EllipseGeometry_vRadii
#elif CHAIN_GEOMETRY == 4
#define vColor HairlineLineGeometry_vColor
#define vEdge HairlineLineGeometry_vEdge
#elif CHAIN_GEOMETRY == 5
#define vColor ComplexEllipseGeometry_vColor
#define vOffsets ComplexEllipseGeometry_vOffsets
#define vRadii ComplexEllipseGeometry_vRadii
#define vEdgeDist ComplexEllipseGeometry_vEdgeDist
#define vStrokeWidth ComplexEllipseGeometry_vStrokeWidth
#elif CHAIN_GEOMETRY == 6
#define vColor RRectGeometry_vColor
#define vLocalCoord RRectGeometry_vLocalCoord
#define vRadii RRectGeometry_vRadii
#define vRectBounds RRectGeometry_vRectBounds
#define vStrokeWidth RRectGeometry_vStrokeWidth
#elif CHAIN_GEOMETRY == 8
#define vColor HairlineQuadGeometry_vColor
#define vHairQuadEdge HairlineQuadGeometry_vHairQuadEdge
#elif CHAIN_GEOMETRY == 7
#define vColor ComplexRRectGeometry_vColor
#define vLocalCoord ComplexRRectGeometry_vLocalCoord
#define vXRadii ComplexRRectGeometry_vXRadii
#define vYRadii ComplexRRectGeometry_vYRadii
#define vRectBounds ComplexRRectGeometry_vRectBounds
#define vStrokeWidth ComplexRRectGeometry_vStrokeWidth
#endif

// Geometry color source: the per-vertex color attribute, or the exact Color uniform for
// common-color draws (the broadcast into the unconditional attribute is 8-bit quantized). The
// selection is a runtime uniform, so a single variant serves both sources.
#define TGFX_CHAIN_GEOM_COLOR \
  ((ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor)

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
// The spec accessors are read here and nowhere else in the chain family: the tool rejects
// SPEC_ outside a file that declares itself a reader, so the specialization surface of this
// family is this file plus the generated include.
#pragma tgfx_spec_reader

#include "pipeline/chain/pointwise_chain_eval.inc"

// The XferProcessor source without coverage: the root, times the coverage subtree root in the
// layouts without a coverage varying (see the epilogue below).
#if GP_LAYOUT >= 1 || HAS_COVERAGE
#define CHAIN_XP_SRC_UNPREMUL result
#else
#define CHAIN_XP_SRC_UNPREMUL \
  (SPEC_CoverageRootIndex() >= 0 ? result * coverageRootRegister : result)
#endif

// One slot's register write-back, shared by the loop and the unrolled executor so the two forms
// cannot disagree: bits 20-24 of the selector carry the result register + 1 (0 = dead
// instruction, skip the write). The value is evaluated before the write, so a consumer may share
// a register with its just-released producer.
#define CHAIN_SLOT_WRITE(n, value)                    \
  {                                                   \
    int outRegister = (SPEC_SlotPacked(n).w >> 20) & 0x1F; \
    if (outRegister != 0) {                           \
      int written = outRegister - 1;                  \
      chainResults[written] = value;                  \
      if (written == SPEC_RootIndex()) {              \
        result = value;                               \
      }                                               \
      if (written == SPEC_ClipCoverageRegister()) {   \
        clipRegister = value;                         \
      }                                               \
      if (written == SPEC_CoverageRootIndex()) {      \
        coverageRootRegister = value;                 \
      }                                               \
    }                                                 \
  }

// Evaluates slot n and writes its register. The unrolled executor calls this with literal n, so a
// specialized compile folds the accessor reads, the op dispatch and the register index to
// constants; the generic compile never calls it (the loop below runs instead).
void chainRunSlotAt(int n, inout vec4 result, inout vec4 clipRegister,
                    inout vec4 coverageRootRegister) {
  vec4 value = evalChainSlot(n);
  CHAIN_SLOT_WRITE(n, value)
}

// The whole evaluation, shared verbatim by the two assemblies: the legacy kernel's main() calls
// it directly, and the declared Chain module's fragment hook calls it from the assembler's main.
void chainMain() {
#if GP_LAYOUT == 1
  // The ellipse edge coverage depends only on varyings, so it is evaluated ahead of the chain:
  // a coverage subtree's unit input consumes it (the -3 designator), and the tail below folds it
  // into the final coverage exactly once (the subtree already carries it, so the coverage-root
  // path must not multiply it in again).
  gpPixelCoverage = ellipseEdgeCoverage(vOffsets, vRadii, ChainColorParams_StrokeEnabled);
#elif GP_LAYOUT == 2
  // The hairline edge coverage (Coverage * edge alpha), with the same contract as the ellipse.
  gpPixelCoverage = hairlineLineEdgeCoverage(vEdge, ChainColorParams_Coverage, ChainColorParams_AAEnabled);
#elif GP_LAYOUT == 3
  // The complex rounded-rect coverage (per-corner radii), same contract as the ellipse;
  // StrokeEnabled selects the fill or the stroke form at runtime.
  gpPixelCoverage = complexEllipseEdgeCoverage(vOffsets, vRadii, vEdgeDist,
                                               vStrokeWidth, ChainColorParams_StrokeEnabled);
#elif GP_LAYOUT == 5
  // The non-AA rounded-rect SDF, hard-stepped exactly like the RRect family.
  gpPixelCoverage =
      nonAARRectCoverage(vLocalCoord, vRectBounds, vRadii, vStrokeWidth,
                         ChainColorParams_StrokeEnabled);
#elif GP_LAYOUT == 7
  // The per-corner-radius rounded-rect SDF, the ComplexRRect family's formula.
  gpPixelCoverage = complexRRectCoverage(vLocalCoord, vRectBounds, vXRadii, vYRadii, vStrokeWidth,
                                         ChainColorParams_StrokeEnabled);
#elif GP_LAYOUT == 9
  // The Loop-Blinn quad edge (Coverage * edge alpha), the same call the dedicated family makes.
  gpPixelCoverage = hairlineQuadEdgeCoverage(vHairQuadEdge, ChainColorParams_Coverage,
                                   ChainColorParams_AAEnabled);
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
  if (SPEC_SlotPacked(0).x == OP_TEXTURE && !chainLeafIsTiled(0)) {
    chainLeafTex[0] = chainPlainLeafFetch(TextureSampler_0, TransformedCoords_0, Subset,
                                          AlphaStart, HasRgbaaa, SubsetClampOn, 0);
  }
  if (SPEC_SlotPacked(1).x == OP_TEXTURE && !chainLeafIsTiled(1)) {
    chainLeafTex[1] = chainPlainLeafFetch(TextureSampler_1, TransformedCoords_1, Subset_1,
                                          AlphaStart_1, HasRgbaaa_1, SubsetClampOn_1, 1);
  }
  if (SPEC_SlotPacked(2).x == OP_TEXTURE && !chainLeafIsTiled(2)) {
    chainLeafTex[2] = chainPlainLeafFetch(TextureSampler_2, TransformedCoords_2, Subset_2,
                                          AlphaStart_2, HasRgbaaa_2, SubsetClampOn_2, 2);
  }
  if (SPEC_SlotPacked(3).x == OP_TEXTURE && !chainLeafIsTiled(3)) {
    chainLeafTex[3] = chainPlainLeafFetch(TextureSampler_3, TransformedCoords_3, Subset_3,
                                          AlphaStart_3, HasRgbaaa_3, SubsetClampOn_3, 3);
  }
  int recipeCount = min(SPEC_SlotCount(), CHAIN_TILED_RECIPES);
  for (int recipe = 0; recipe < recipeCount; ++recipe) {
    int leaf = chainTiledRecipeLeaf(recipe);
    if (leaf >= 0 && leaf < NTEX && SPEC_SlotPacked(leaf).x == OP_TEXTURE) {
      chainLeafTex[leaf] = chainTiledLeafFetch(leaf);
    }
  }
#endif
  // The three registers the epilogue consumes (root, clip coverage, coverage subtree root) are
  // captured as the loop writes them instead of being read back as chainResults[<uniform>] after
  // the loop. Registers are reused, so "the last write to that register" is exactly what the
  // read-back returned. The capture is required, not cosmetic: on the Apple desktop GL driver, the
  // zero-coverage discard in applyPorterDuffXP (xp_porter_duff.inc) killed every fragment of the
  // draw while the coverage came from that read-back, although the same expression selected the
  // correct colour when no discard was present. Capturing inside the loop avoids it.
  vec4 result = vec4(0.0);
  vec4 clipRegister = vec4(0.0);
  vec4 coverageRootRegister = vec4(0.0);
#if HAS_XP >= 1
  // With an advanced XferProcessor mode the loop runs one pass past the instructions: slot
  // SlotCount blends the XferProcessor source with the dst at evalChainSlot's OP_BLEND site
  // (xp_blend_pass.inc). The pass count stays uniform data, so the loop is not unrolled.
  chainXPDst = xpReadDstColor();
  vec4 xpBlended = vec4(0.0);
#endif
// The unrolled executor rides behind a tool-set macro: the stub pass (reflection and the inline
// policy) compiles the loop alone, while the generic and the spec forms compile both — the
// generic GL driver then folds the branch away through the master-switch constant. The default
// keeps the unrolled form in whenever the macro is not injected, so a bare compile never loses
// the specialization machinery.
#ifndef TGFX_SPEC_UNROLLED_EXECUTOR
#define TGFX_SPEC_UNROLLED_EXECUTOR 1
#endif
#if TGFX_SPEC_UNROLLED_EXECUTOR
  if (SPEC_ACTIVE()) {
    // The specialized executor: one call site per slot with literal indices. Once the runtime
    // injects the values, every guard and every selector word becomes a compile-time constant, so
    // the driver deletes the out-of-range call sites entirely.
    // One guarded call site per slot of the declared capacity, generated from the module's
    // UnrollSite declaration (one line per slot of the variant's own capacity).
    Chain_RUN_UNROLLED_SLOTS()
#if HAS_XP >= 1
    if (xpNeedsBlendPass()) {
      chainXPSrc = CHAIN_XP_SRC_UNPREMUL;
      xpBlended = evalChainSlot(SPEC_SlotCount());
    }
#endif
  } else
#endif
  {
    // The generic executor: the runtime loop over uniform data. The compiler cannot unroll it
    // because the bound is uniform data; this is the zero-compile path every unspecialized
    // compile runs.
#if HAS_XP >= 1
    int passCount = SPEC_SlotCount() + (xpNeedsBlendPass() ? 1 : 0);
#else
    int passCount = SPEC_SlotCount();
#endif
    for (int i = 0; i < passCount; ++i) {
#if HAS_XP >= 1
      if (i == SPEC_SlotCount()) {
        chainXPSrc = CHAIN_XP_SRC_UNPREMUL;
      }
#endif
      vec4 value = evalChainSlot(i);
#if HAS_XP >= 1
      if (i == SPEC_SlotCount()) {
        xpBlended = value;
      } else
#endif
      {
        CHAIN_SLOT_WRITE(i, value)
      }
    }
  }

  // The narrow clip slots chain from the scalar unit one (-4) instead of multiplying the color
  // root, so their product is the pure clip coverage (see ClipCoverageRegister above). The atlas
  // path routes its glyph-mask subtree root through the same register: the glyph mask is TRUE
  // coverage in the runtime composite (the dedicated MaskFill shader carries it as
  // TGFX_XP_COVERAGE), unlike a MaskFilter's source-modulating mask, whose subtree keeps the
  // CoverageRootIndex source route (MaskCoverageBlendModesOnOpaqueBackground, byte-parity across
  // Src/SrcOver/Multiply/Darken).
  vec4 clipCoverage = SPEC_ClipCoverageRegister() >= 0 ? clipRegister : vec4(1.0);

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
  highp vec4 finalCoverage =
      SPEC_CoverageRootIndex() >= 0 ? coverageRootRegister : TGFX_CHAIN_UNIT_COVERAGE;
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



#endif  // TGSL_POINTWISE_FRAGMENT_MAIN_INCLUDED
