// Fragment stage of the pointwise kernels: shared core, part 3 of 3, leaf fetch, slot evaluation
// and main(). The entry defines chainLoadTiledRecipe() (through its tiling module) and
// evalResourceSlot() before including this file.

#ifndef TGSL_POINTWISE_FRAGMENT_MAIN_INCLUDED
#define TGSL_POINTWISE_FRAGMENT_MAIN_INCLUDED

#if NTEX > 0
#include "entry/ChainLeafFetch.glsl"

// Tiled leaf fetch: the shared tiling math, so wrap and clamp-to-border modes match the runtime.
// This is the heavy part of a leaf fetch (coordinate mapping, a seam blend with up to three extra
// reads, the border post-pass). main() calls it from a single site inside a loop over the leaves,
// reading the leaf's texture through aotChainRead's per-index sampler pick, so the tiling code
// exists once in the binary instead of once per leaf (a GPU compiler inlines every call site).
#pragma tgfx_spec_gate TiledFetch : (SPEC_TextureFlags() & 2) != 0
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
#pragma tgfx_spec_gate TiledTest : (SPEC_TextureFlags() & 2) != 0
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

// The geometry's coverage: the hook-contract layouts copy it in from Fragment::coverage (see
// Chain_fragment), the aliased layouts compute their per-pixel GP edge coverage ahead of the chain
// evaluation (GP_LAYOUT 1, 2, 3, 5, 7, 9: ellipse, hairline-line, complex rounded rect, the non-AA
// rounded rects and the hairline quad). 1.0 keeps the value valid for the layouts without coverage.
highp float gpPixelCoverage = 1.0;

// The geometry's color, written by the hook-contract layouts (see Chain_fragment); the aliased
// layouts read their varying through TGFX_CHAIN_GEOM_COLOR instead.
vec4 chainGeomColor = vec4(0.0);

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

// Whether the geometry contributes true coverage: the epilogue replaces it with the coverage
// subtree's root instead of modulating the source with it. The hook-contract layouts declare the
// fact themselves; the aliased layouts derive it from the layout class they encode.
#ifdef CHAIN_GEOMETRY_HOOK_CONTRACT
#if CHAIN_GEOMETRY_OWNS_COVERAGE
#define CHAIN_HAS_GEOMETRY_COVERAGE 1
#else
#define CHAIN_HAS_GEOMETRY_COVERAGE 0
#endif
#elif GP_LAYOUT >= 1 || HAS_COVERAGE
#define CHAIN_HAS_GEOMETRY_COVERAGE 1
#else
#define CHAIN_HAS_GEOMETRY_COVERAGE 0
#endif

// Geometry color source: the hook contract's value, or — for the aliased layouts — the per-vertex
// color attribute, or the exact Color uniform for common-color draws (the broadcast into the
// unconditional attribute is 8-bit quantized). The selection is a runtime uniform, so a single
// variant serves both sources.
#ifdef CHAIN_GEOMETRY_HOOK_CONTRACT
#define TGFX_CHAIN_GEOM_COLOR chainGeomColor
#else
#define TGFX_CHAIN_GEOM_COLOR \
  ((ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor)
#endif

// The coverage subtree's unit input (slot designator -3): the GP's output coverage, which is
// what the runtime coverage chain starts from. The hook-contract layouts hand it in through
// gpPixelCoverage; the aliased rect layouts carry it as the vCoverage varying, and the ellipse and
// hairline layouts evaluate their per-pixel edge coverage ahead of the chain (see gpPixelCoverage).
// The vec4(1.0) form keeps the define valid for the remaining variants.
#ifdef CHAIN_GEOMETRY_HOOK_CONTRACT
#define TGFX_CHAIN_UNIT_COVERAGE vec4(gpPixelCoverage)
#elif GP_LAYOUT == 0 && HAS_COVERAGE
#define TGFX_CHAIN_UNIT_COVERAGE vec4(vCoverage)
#elif GP_LAYOUT >= 1
#define TGFX_CHAIN_UNIT_COVERAGE vec4(gpPixelCoverage)
#else
#define TGFX_CHAIN_UNIT_COVERAGE vec4(1.0)
#endif

// The fixed designator sources as functions: the build expands the macros above per variant, so a
// runtime-emitted body cannot spell them — a function's name survives the expansion while its body
// carries the variant's form. The interpreter's designator arms call them too, so the runtime's
// uncalled-function pruning keeps them in every specialized text (see ChainBodyEmitter).
vec4 chainGeomColorSource() {
  return TGFX_CHAIN_GEOM_COLOR;
}

vec4 chainUnitCoverageSource() {
  return TGFX_CHAIN_UNIT_COVERAGE;
}

vec4 chainOpaqueGeomColorSource() {
  return vec4(TGFX_CHAIN_GEOM_COLOR.rgb, 1.0);
}

// The spec accessors are read here and nowhere else in the chain family: the tool rejects
// SPEC_ outside a file that declares itself a reader, so the specialization surface of this
// family is this file plus the generated include.
#pragma tgfx_spec_reader

#include "pipeline/chain/pointwise_chain_eval.inc"

// The XferProcessor source without coverage: the root, times the coverage subtree root in the
// layouts whose geometry contributes no coverage (see the epilogue below).
#if CHAIN_HAS_GEOMETRY_COVERAGE
#define CHAIN_XP_SRC_UNPREMUL result
#else
#define CHAIN_XP_SRC_UNPREMUL \
  (SPEC_CoverageRootIndex() >= 0 ? result * coverageRootRegister : result)
#endif

// The XferProcessor blend pass's source, as a function for the same reason as the designator
// sources above: the macro's expansion differs per variant, and only the function's name survives
// into the stored text. The interpreter's blend pass calls it too, so the runtime's uncalled
// function pruning keeps it in every specialized text.
vec4 chainXPSrcUnpremul(vec4 result, vec4 coverageRootRegister) {
  return CHAIN_XP_SRC_UNPREMUL;
}

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

// The whole evaluation, shared verbatim by the two assemblies: the interpreter's main() calls
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
#if HAS_SUBSET
#define CHAIN_QUAD_SUBSET_APPLY 1
#define CHAIN_QUAD_SUBSET vTexSubset
#else
#define CHAIN_QUAD_SUBSET_APPLY 0
#define CHAIN_QUAD_SUBSET vec4(0.0)
#endif
  if (SPEC_SlotPacked(0).x == OP_TEXTURE && !chainLeafIsTiled(0)) {
    chainLeafTex[0] = chainLeafFetch(TransformedCoords_0, Subset, AlphaStart, HasRgbaaa,
                                     SubsetClampOn, 0, DeviceLeafMask, DeviceLeafCoordMatrix[0],
                                     LeafReadSwizzles[0], gl_FragCoord.xy, CHAIN_QUAD_SUBSET_APPLY,
                                     CHAIN_QUAD_SUBSET);
  }
  if (SPEC_SlotPacked(1).x == OP_TEXTURE && !chainLeafIsTiled(1)) {
    chainLeafTex[1] = chainLeafFetch(TransformedCoords_1, Subset_1, AlphaStart_1, HasRgbaaa_1,
                                     SubsetClampOn_1, 1, DeviceLeafMask, DeviceLeafCoordMatrix[1],
                                     LeafReadSwizzles[1], gl_FragCoord.xy, 0, vec4(0.0));
  }
  if (SPEC_SlotPacked(2).x == OP_TEXTURE && !chainLeafIsTiled(2)) {
    chainLeafTex[2] = chainLeafFetch(TransformedCoords_2, Subset_2, AlphaStart_2, HasRgbaaa_2,
                                     SubsetClampOn_2, 2, DeviceLeafMask, DeviceLeafCoordMatrix[2],
                                     LeafReadSwizzles[2], gl_FragCoord.xy, 0, vec4(0.0));
  }
  if (SPEC_SlotPacked(3).x == OP_TEXTURE && !chainLeafIsTiled(3)) {
    chainLeafTex[3] = chainLeafFetch(TransformedCoords_3, Subset_3, AlphaStart_3, HasRgbaaa_3,
                                     SubsetClampOn_3, 3, DeviceLeafMask, DeviceLeafCoordMatrix[3],
                                     LeafReadSwizzles[3], gl_FragCoord.xy, 0, vec4(0.0));
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
  // The composed-executor splice markers: a specialized GLSL text whose slot structure is fully
  // known replaces everything between the two pragmas with one straight-line evaluation (see
  // ChainBodyEmitter). They are pragmas, not comments, because the emitter strips line comments —
  // pragmas are the one directive that survives into the stored text and every driver ignores.
#pragma tgfx_composed_executor_begin
  {
    // The executor: one loop over the slots, one dispatch copy. The unrolled form that emitted a
    // call site per slot let the binary backends inline a full dispatch copy into each; it
    // measured 7x the Metal bundle (10.46MB -> 1.53MB) and 40% of its first-program time with no
    // hot-path gain on Metal or GL, so it is deleted rather than kept behind a flag.
#if HAS_XP >= 1
    int passCount = SPEC_SlotCount() + (xpNeedsBlendPass() ? 1 : 0);
#else
    int passCount = SPEC_SlotCount();
#endif
    for (int i = 0; i < passCount; ++i) {
#if HAS_XP >= 1
      if (i == SPEC_SlotCount()) {
        chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);
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
#pragma tgfx_composed_executor_end

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
#if CHAIN_HAS_GEOMETRY_COVERAGE
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
