// Core fields of the pointwise kernels' FragmentUniformBlock, included inside the block by the
// kernel entry ahead of the resource modules' fields (see pointwise_fragment_config.inc).
  vec4 Color;
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
  vec4 Subset;
#if NTEX >= 2
  vec4 Subset_1;
#endif
#if NTEX >= 4
  vec4 Subset_2;
  vec4 Subset_3;
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
  int ChainCSFlags;
  vec4 ChainSrcTF0;
  vec4 ChainSrcTF1;
  int ChainSrcTFType;
  vec4 ChainSrcOOTF;
  mat3 ChainColorXform;
  vec4 ChainDstOOTF;
  vec4 ChainDstTF0;
  vec4 ChainDstTF1;
  int ChainDstTFType;
  // Chain-wide AA-rect clip for OP_AARECT_COVERAGE (at most one such slot per chain). The rect is
  // in destination device coordinates and already carries the 0.5 outset for the AA falloff.
  vec4 CoverageRect;
  // Chain-wide local-space rect clip for OP_LOCAL_RECT_COVERAGE (at most one such slot per
  // chain). CoverageLocalRect keeps exact coordinates; the kernel maps gl_FragCoord through
  // CoverageLocalDeviceToLocal before the half-pixel AA ramp.
  vec4 CoverageLocalRect;
  mat3 CoverageLocalDeviceToLocal;
  // RRect clip parameter arrays for OP_RRECT_COVERAGE, indexed by the slot's rrect ordinal
  // (selector bits 16-19); at most four such slots per chain.
  vec4 CoverageRRectRect[4];
  vec4 CoverageRRectRadiiX[4];
  vec4 CoverageRRectRadiiY[4];
  float CoverageRRectAntiAlias[4];
  mat3 CoverageRRectDeviceToLocal[4];
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
#endif

#include "pointwise_chain_uniforms.inc"
