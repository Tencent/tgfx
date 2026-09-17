// Declares the chain instructions' uniform fields as std140 arrays inside the FragmentUniformBlock,
// so the instruction evaluation can be a single shared function parameterized by instruction index.
// Included once. Per-instruction footprint: Packed (op, two input register indices, selector),
// ConstColor, ColorMatrix + ColorVector, and LumaThresh (luma rgb / alpha-threshold a in one vec4).
// The array bound is the kernel's instruction capacity (AOTPointwiseChainProcessor::MaxSlots); the
// result registers live in the fixed-size chainResults array instead, recycled by last use.
// std140 layout is derived from this order; keep it in sync with
// AOTPointwiseChainProcessor::onSetData.
  ivec4 SlotPacked[32];
  vec4 SlotConstColor[32];
  mat4 SlotColorMatrix[32];
  vec4 SlotColorVector[32];
  vec4 SlotLumaThresh[32];
