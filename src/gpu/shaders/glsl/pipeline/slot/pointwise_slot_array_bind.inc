// Binds pointwise_op.inc's operator field names onto the chain instruction arrays (the YUV
// kernel's color-grade tail): one slot encoding system-wide, the same records the chain
// interpreter reads. The selector (SlotPacked[slot].w) packs the op's small value in bits 0-7
// (the const-color input mode, the blend mode) and the blend's operand order in bit 8 — the
// YUV binder writes exactly these bits. The color-space fields bind to the shared Chain block
// (at most one transform per tail, like the interpreter's). Include after the
// FragmentUniformBlock and before pointwise_op.inc, then include pointwise_slot_array_unbind.inc
// afterwards. The self-referential expansions are safe: the preprocessor does not rescan the
// macro's own name.
#define OpType (SlotPacked[slot].x)
#define ColorMatrix SlotColorMatrix[slot]
#define ColorVector SlotColorVector[slot]
#define Kr SlotLumaThresh[slot].x
#define Kg SlotLumaThresh[slot].y
#define Kb SlotLumaThresh[slot].z
#define Threshold SlotLumaThresh[slot].w
#define CSFlags ChainCSFlags
#define SrcTF0 ChainSrcTF0
#define SrcTF1 ChainSrcTF1
#define SrcTFType ChainSrcTFType
#define SrcOOTF ChainSrcOOTF
#define ColorXform ChainColorXform
#define DstOOTF ChainDstOOTF
#define DstTF0 ChainDstTF0
#define DstTF1 ChainDstTF1
#define DstTFType ChainDstTFType
#define ConstColorValue SlotConstColor[slot]
#define ConstInputMode (SlotPacked[slot].w & 0xFF)
#define BlendModeValue (SlotPacked[slot].w & 0xFF)
#define BlendConstFirst ((SlotPacked[slot].w >> 8) & 1)
