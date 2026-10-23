// Chain slot op codes and ColorSpaceXform flags of the pointwise kernels. Op values mirror
// AOTChainOp and are part of the precompiled kernel ABI (validated against
// KernelContract.h ChainOp by the shader build tool). Included by the slot evaluation and by every
// resource module that dispatches on an op.
#ifndef TGFX_CHAIN_OP_CONSTANTS
#define TGFX_CHAIN_OP_CONSTANTS
#define OP_COLOR_MATRIX 0
#define OP_LUMA 1
#define OP_ALPHA_THRESHOLD 2
#define OP_COLOR_SPACE_XFORM 3
#define OP_NONE 4
#define OP_TEXTURE 5
#define OP_CONST_COLOR 6
#define OP_BLEND 7
#define OP_TEX_MODULATE 12
#define OP_INPUT_OPAQUE 13
#define OP_MUL_ALPHA 14
#define OP_PERLIN_NOISE 15
#define OP_AARECT_COVERAGE 8
#define OP_GRADIENT 9
#define OP_LOCAL_RECT_COVERAGE 10
#define OP_RRECT_COVERAGE 11
#endif
