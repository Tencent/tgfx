// Core fields of the pointwise kernels' FragmentUniformBlock, included inside the block by the
// kernel entry ahead of the resource modules' fields (see pointwise_fragment_config.inc).
  vec4 Color;
  // Selects the geometry color source: the exact Color uniform when set, the per-vertex vColor
  // varying otherwise (the broadcast into the unconditional attribute is 8-bit quantized, so the
  // common color prefers the uniform; written by each GP's onSetData).
  int HasCommonColor;
  int RootIndex;
  // Slot index of the coverage subtree's root, -1 when the chain has none. When present, the
  // subtree's value replaces the plain coverage modulation (it already carries the GP coverage
  // through the -3 unit input).
  int CoverageRootIndex;
  // Register index of the narrow clip slots' product, -1 when the chain has none. The clip slots
  // chain from the scalar unit one (-4) instead of multiplying the color root, so this value is
  // the pure clip coverage: it rides the XP's coverage input together with the GP coverage and
  // the device mask (and the atlas subtree's root, which the builder routes here because the
  // glyph mask is true coverage), while the source stays unpremultiplied — compositing correctly
  // for every blend mode (the kernel's xpBlendWithCoverage carries the runtime's per-mode forms;
  // folding coverage into the source was only correct for SrcOver).
  int ClipCoverageRegister;
  int SlotCount;
#if NTEX > 0
  ivec4 LeafReadSwizzles;
#endif
  // Per-leaf subset rects, named for the structural ordinals the TextureEffect writers use.
  // SubsetClampOn gates each leaf's clamp exactly as the fill pipeline does (and as the runtime
  // TextureEffect's needSubset() does): an ungated clamp pins wrap-tiled coordinates to the subset
  // rect and zeroes their mip derivatives, which is why the tiled and device leaves used to need an
  // unbounded sample-area decoy. A leaf whose gate is 0 reads its hardware sampler unclamped.
  vec4 Subset;
  int SubsetClampOn;
#if NTEX >= 2
  vec4 Subset_1;
  int SubsetClampOn_1;
#endif
#if NTEX >= 4
  vec4 Subset_2;
  int SubsetClampOn_2;
  vec4 Subset_3;
  int SubsetClampOn_3;
#endif
#if NTEX > 0
  // Per-leaf RGBAAA layout (Image::makeRGBAAA), named for the same structural ordinals: the
  // TextureEffect writer sets HasRgbaaa for every leaf and AlphaStart (the alpha area's offset in
  // texture coordinates) for an RGBAAA one. Sampler-only children write HasRgbaaa = 0 too.
  vec2 AlphaStart;
  int HasRgbaaa;
  vec2 AlphaStart_1;
  int HasRgbaaa_1;
  vec2 AlphaStart_2;
  int HasRgbaaa_2;
  vec2 AlphaStart_3;
  int HasRgbaaa_3;
#endif
  // Shared ColorSpaceXform parameters; at most one slot per chain may be OP_COLOR_SPACE_XFORM.
#include "pipeline/chain/pointwise_cs_uniforms.inc"
  // The per-slot data pool: TGFX_CHAIN_DATA_SLOT_WORDS vec4 words per slot, holding whichever
  // operator's parameters that slot carries. Each operator's layout exists only in ChainOps.h,
  // and the readers the instruction table calls come from the same declarations.
  vec4 ChainData[CHAIN_SLOT_CAPACITY * TGFX_CHAIN_DATA_SLOT_WORDS];
#if NTEX > 0
  // Four-leaf variants always bind the mask sampler (a phantom when the chain has no mask);
  // HasMaskTexture selects the application at runtime.
  mat3 DeviceCoordMatrix;
  // Decoy write target for the coverage mask FP (see GLSLDeviceSpaceTextureEffect::onSetData);
  // never read — the mask is sampled unclamped.
  vec4 DeviceMaskSubset;
  int HasMaskTexture;
  // The leaves sampled in destination device space (DeviceSpaceTextureEffect sources): bit k set
  // makes leaf k read DeviceLeafCoordMatrix[k] * gl_FragCoord instead of its TransformedCoords
  // varying. Zero when the chain has none.
  int DeviceLeafMask;
  mat3 DeviceLeafCoordMatrix[4];
#elif HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
  // Decoy write target for the coverage mask FP (see GLSLDeviceSpaceTextureEffect::onSetData);
  // never read — the mask is sampled unclamped.
  vec4 DeviceMaskSubset;
#endif
#if GP_LAYOUT == 1
  // EllipseGeometryProcessor writes this for every ellipse draw (stroke vs fill coverage).
  int StrokeEnabled;
#elif GP_LAYOUT == 2
  // HairlineLineGeometryProcessor writes both for every hairline draw: the coverage scale and
  // whether the edge ramp is antialiased (otherwise stepped at 0.5).
  float Coverage;
  int AAEnabled;
#endif

#include "pipeline/chain/pointwise_chain_uniforms.inc"

  // Alpha-only render target (Swizzle::ForWrite(ALPHA_8) == AAAA): 1 makes main() write
  // fragColor.aaaa through output_swizzle.inc, so one variant serves both formats — the same
  // runtime uniform SolidColorFill/MaskFill/GaussianBlur1D already use.
  int OutputAlphaSwizzle;
