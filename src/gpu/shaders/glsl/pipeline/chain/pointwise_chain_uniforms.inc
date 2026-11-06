// Declares the chain instructions' uniform fields as std140 arrays inside the FragmentUniformBlock,
// so the instruction evaluation can be a single shared function parameterized by instruction index.
// Included once. Per-instruction footprint: SlotPacked (op, two input register indices, selector)
// and one data-pool block of TGFX_CHAIN_DATA_SLOT_WORDS vec4 words.
// CHAIN_SLOT_CAPACITY is 16 for the base chain and the gradient, 32 for the noise shape. The
// result registers live in the fixed-size chainResults array, recycled by last use. std140 layout
// is derived from this order, but all access goes through the field names via reflection, so the
// order is a layout convenience only (the binder and the capture both resolve offsets by name).
//
// Parameter allocation table. A slot holds exactly one operator (SlotPacked.x is its op), so the
// whole parameter area of a slot belongs to that operator — nothing is shared between operators,
// and a new operator picks a free register here instead of borrowing another operator's field:
//   SlotPacked  the instruction word: op, in0, in1, selector bits
//   SlotMat     ColorMatrix: the 4x4 linear part (16 floats)
//   SlotVec0    ColorMatrix: the translate vector (4 floats)
//   SlotVec1    ConstColor: the constant; Blend: the constant operand
//   SlotVec2    Luma: .rgb; AlphaThreshold: .a
  ivec4 SlotPacked[CHAIN_SLOT_CAPACITY];
  // The per-slot parameter pool: TGFX_CHAIN_DATA_SLOT_WORDS vec4 words per slot, holding whichever
  // operator's parameters that slot carries. Each operator's layout exists only in ChainOps.h and
  // the readers come from the same declarations.
  vec4 ChainData[CHAIN_SLOT_CAPACITY * TGFX_CHAIN_DATA_SLOT_WORDS + TGFX_CHAIN_BLOCK_WORDS];
