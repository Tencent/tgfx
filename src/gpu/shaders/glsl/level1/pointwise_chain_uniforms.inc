// Declares one chain slot's uniform fields inside a FragmentUniformBlock. Include once per slot
// with pointwise_chain_bind.inc redirecting every name to that slot's flat field (Slot0Packed,
// ...). The reflection extractor reads flat top-level members only, so slots are distinct flat
// fields, and the bundle format caps a stage at 255 uniforms — the per-slot footprint stays at 5
// fields so 16 slots plus the shared blocks fit.
//
// std140 layout is derived from this order; keep it in sync with
// AOTPointwiseChainProcessor::onSetData.
  // x = op (AOTChainOp), y/z = input slot indices (-1 = geometry Color, -2 = none),
  // w = BlendMode for OP_BLEND and ConstInputMode for OP_CONST_COLOR.
  ivec4 Packed;
  // ConstColor op
  vec4 ConstColor;
  // ColorMatrix op
  mat4 ColorMatrix;
  vec4 ColorVector;
  // Luma op (rgb) and AlphaThreshold op (a), packed into one vec4.
  vec4 LumaThresh;
