// Declares the chain instructions' uniform fields as std140 arrays inside the FragmentUniformBlock,
// so the instruction evaluation can be a single shared function parameterized by instruction index.
// Included once. Per-instruction footprint: Packed (op, two input register indices, selector),
// ConstColor, ColorMatrix + ColorVector, and LumaThresh (luma rgb / alpha-threshold a in one vec4).
// CHAIN_SLOT_CAPACITY is 16 for the base chain and 32 for the extended graph. The result
// registers live in the fixed-size chainResults array, recycled by last use. std140 layout is
// derived from this order; keep it in sync with AOTPointwiseChainProcessor::onSetData.
  ivec4 SlotPacked[CHAIN_SLOT_CAPACITY];
  vec4 SlotConstColor[CHAIN_SLOT_CAPACITY];
  mat4 SlotColorMatrix[CHAIN_SLOT_CAPACITY];
  vec4 SlotColorVector[CHAIN_SLOT_CAPACITY];
  vec4 SlotLumaThresh[CHAIN_SLOT_CAPACITY];
