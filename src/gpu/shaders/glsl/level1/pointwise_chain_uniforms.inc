// Declares the chain slots' uniform fields as std140 arrays inside the FragmentUniformBlock, so
// the slot evaluation can be a single shared function parameterized by slot index. Included once.
// Per-slot footprint: Packed (op, two input slot indices, blend/const selector), ConstColor,
// ColorMatrix + ColorVector, and LumaThresh (luma rgb / alpha-threshold a in one vec4).
// std140 layout is derived from this order; keep it in sync with
// AOTPointwiseChainProcessor::onSetData.
  ivec4 SlotPacked[16];
  vec4 SlotConstColor[16];
  mat4 SlotColorMatrix[16];
  vec4 SlotColorVector[16];
  vec4 SlotLumaThresh[16];
