// Fragment stage of the pointwise kernels: shared core, part 3 of 3, leaf fetch, slot evaluation
// and main(). The entry defines chainLoadTiledRecipe() (through its tiling module) and
// evalResourceSlot() before including this file.

// Leaf fetch: plain leaves clamp to their Subset rect (full bounds = no-op). The tiled leaf, if
// any, goes through the shared tiling math so wrap and clamp-to-border modes match the runtime.
vec4 chainLeafFetch(CHAIN_LEAF_SAMPLER texSampler, vec3 coord, vec4 leafSubset, int leafIndex) {
  // Perspective divide: affine transforms produce z=1.0, making this a no-op (same contract as
  // QuadTextureFillShader); the tiled/subset math below then runs on the divided coordinate,
  // matching the runtime's emitPerspTextCoord ordering.
  highp vec2 uv = coord.xy / coord.z;
  int readSwizzle = 0;
#if NTEX > 0
  readSwizzle = LeafReadSwizzles[leafIndex];
  // A device-space leaf samples at the fragment's destination position, like the runtime
  // DeviceSpaceTextureEffect. Its Subset is unbounded, so the clamp below is a no-op for it.
  if (leafIndex == DeviceLeafIndex) {
    highp vec3 deviceCoord = DeviceLeafCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
    uv = deviceCoord.xy;
  }
#endif
  bool strict = false;
  if (chainLoadTiledRecipe(leafIndex, strict)) {
    vec2 inCoord = vec2(0.0);
    vec2 subsetCoord = vec2(0.0);
    vec2 clampedCoord = vec2(0.0);
    vec2 sampleCoord = tiledMapCoord(uv, strict, inCoord, subsetCoord, clampedCoord);
    vec4 texColor = aotChainRead(texSampler, sampleCoord, readSwizzle);
    texColor = tiledSeamBlend(texSampler, texColor, subsetCoord, clampedCoord, readSwizzle);
    return tiledApplyBorder(texColor, inCoord, subsetCoord, clampedCoord);
  }
  return aotChainRead(texSampler, clamp(uv, leafSubset.xy, leafSubset.zw), readSwizzle);
}

layout(location = 0) out vec4 fragColor;

vec4 chainResults[16];

// Prefetched leaf samples. Fetched with statically bound samplers in main; the OP_TEXTURE branch
// of evalChainSlot reads them back by instruction index.
vec4 chainLeafTex[4];

// The ellipse layout's per-pixel edge coverage (GP_LAYOUT=1). Computed ahead of the chain
// evaluation in main so the coverage subtree's unit input can consume it like the rect
// layouts' vCoverage varying; 1.0 keeps the define valid for every other variant.
highp float ellipseGpCoverage = 1.0;

// Geometry color source: the unconditional inColor attribute on QuadGP (broadcast for
// common-color draws), the Color uniform otherwise.
#if HAS_COLOR
#define TGFX_CHAIN_GEOM_COLOR vColor
#else
#define TGFX_CHAIN_GEOM_COLOR Color
#endif

// The coverage subtree's unit input (slot designator -3): the GP's output coverage, which is
// what the runtime coverage chain starts from. The rect layouts carry it as the vCoverage
// varying; the ellipse layout evaluates its per-pixel edge coverage ahead of the chain (see
// ellipseGpCoverage). The vec4(1.0) form keeps the define valid for the remaining variants.
#if GP_LAYOUT == 0 && HAS_COVERAGE
#define TGFX_CHAIN_UNIT_COVERAGE vec4(vCoverage)
#elif GP_LAYOUT == 1
#define TGFX_CHAIN_UNIT_COVERAGE vec4(ellipseGpCoverage)
#else
#define TGFX_CHAIN_UNIT_COVERAGE vec4(1.0)
#endif
#include "pointwise_chain_eval.inc"

void main() {
#if GP_LAYOUT == 1
  // The ellipse edge coverage depends only on varyings, so it is evaluated ahead of the chain:
  // a coverage subtree's unit input consumes it (the -3 designator), and the tail below folds it
  // into the final coverage exactly once (the subtree already carries it, so the coverage-root
  // path must not multiply it in again).
  ellipseGpCoverage = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);
#endif
  // Active slots are contiguous from 0 by construction (texture leaves first, then the rest in
  // topological order), so a uniform guard skips the whole evaluation of unused slots —
  // including the Packed read — instead of relying on the OP_NONE early-out alone.
  // Texture leaves occupy slots 0..NTEX-1. Each prefetch is guarded by the slot's op marker so
  // the phantom slot of a 3-leaf chain and the sampler-only LUT child (both non-texture slots
  // occupying leaf positions) are never sampled; the loop below still evaluates them like any
  // other slot.
#if NTEX > 0
  if (SlotPacked[0].x == OP_TEXTURE) {
    chainLeafTex[0] = chainLeafFetch(TextureSampler_0, TransformedCoords_0, Subset, 0);
  }
#endif
#if NTEX > 1
  if (SlotPacked[1].x == OP_TEXTURE) {
    chainLeafTex[1] = chainLeafFetch(TextureSampler_1, TransformedCoords_1, Subset_1, 1);
  }
#endif
#if NTEX > 2
  if (SlotPacked[2].x == OP_TEXTURE) {
    chainLeafTex[2] = chainLeafFetch(TextureSampler_2, TransformedCoords_2, Subset_2, 2);
  }
#endif
#if NTEX > 3
  if (SlotPacked[3].x == OP_TEXTURE) {
    chainLeafTex[3] = chainLeafFetch(TextureSampler_3, TransformedCoords_3, Subset_3, 3);
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
  for (int i = 0; i < SlotCount; ++i) {
    // Bits 20-24 of the selector carry the result register + 1 (0 = dead instruction, skip the
    // write). The value is evaluated before the write, so a consumer may share a register with
    // its just-released producer.
    vec4 value = evalChainSlot(i);
    int outRegister = (SlotPacked[i].w >> 20) & 0x1F;
    if (outRegister != 0) {
      int written = outRegister - 1;
      chainResults[written] = value;
      if (written == RootIndex) {
        result = value;
      }
      if (written == ClipCoverageRegister) {
        clipRegister = value;
      }
      if (written == CoverageRootIndex) {
        coverageRootRegister = value;
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
  vec4 clipCoverage = ClipCoverageRegister >= 0 ? clipRegister : vec4(1.0);

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

#if GP_LAYOUT == 1
  // Ellipse edge AA: the per-pixel coverage was evaluated ahead of the chain (ellipseGpCoverage).
  // A coverage subtree's root already carries it (its unit input is it), so the root path folds
  // in only the device mask; the bare path multiplies the whole edge coverage once.
  highp vec4 ellipseFinalCoverage = vec4(deviceMask * ellipseGpCoverage);
  if (CoverageRootIndex >= 0) {
    ellipseFinalCoverage = coverageRootRegister * vec4(deviceMask);
  }
  ellipseFinalCoverage *= clipCoverage;
#define TGFX_XP_SRC_COLOR (result * ellipseFinalCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE ellipseFinalCoverage
#elif HAS_COVERAGE
  // A coverage subtree's root already carries the GP coverage (fed in through the -3 unit
  // input), so it replaces the plain vCoverage modulation instead of doubling it.
  vec4 finalCoverage = CoverageRootIndex >= 0 ? coverageRootRegister : vec4(vCoverage);
  finalCoverage *= deviceMask;
  finalCoverage *= clipCoverage;
#define TGFX_XP_SRC_COLOR (result * finalCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE finalCoverage
#else
  // The no-coverage-varying layouts previously left TGFX_XP_COVERAGE at its vec4(1.0) default
  // with every coverage form (clips, device mask, the coverage subtree) premultiplied into the
  // source — composites correctly only for SrcOver-class blending. The TRUE coverage forms (the
  // clip product and the device mask) now ride C with the unpremultiplied source, compositing
  // through the per-mode forms like every other layout (the clip-fold and atlas defects). A MaskFilter's
  // coverage subtree stays in the source: the runtime itself modulates the source with it, so
  // blend(c*S, D) — not c*blend(S,D) + (1-c)*D — is the matching semantics there.
  vec4 noCovCoverage = clipCoverage * vec4(deviceMask);
#define TGFX_XP_SRC_COLOR                                                       \
  (CoverageRootIndex >= 0 ? result * coverageRootRegister * noCovCoverage \
                          : result * noCovCoverage)
#define TGFX_XP_SRC_UNPREMUL (CoverageRootIndex >= 0 ? result * coverageRootRegister : result)
#define TGFX_XP_COVERAGE noCovCoverage
#endif
#include "xp_output.inc"
}
