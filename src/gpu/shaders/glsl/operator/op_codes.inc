// The single op table of the pointwise interpreters (chain and slot). Values are anchored to the
// Op constants in KernelContract.h and validated by the shader build tool; no other GLSL file may
// define an OP_* macro. The slot interpreter dispatches on the subset it supports
// (AOTPointwiseOpType), but every consumer includes the whole table so one op has one value
// everywhere.
#ifndef TGFX_OP_CODES_INC
#define TGFX_OP_CODES_INC
#define OP_COLOR_MATRIX 0
#define OP_LUMA 1
#define OP_ALPHA_THRESHOLD 2
#define OP_COLOR_SPACE_XFORM 3
#define OP_NONE 4
#define OP_TEXTURE 5
#define OP_CONST_COLOR 6
#define OP_BLEND 7
#define OP_AARECT_COVERAGE 8
#define OP_GRADIENT 9
#define OP_LOCAL_RECT_COVERAGE 10
#define OP_RRECT_COVERAGE 11
#define OP_TEX_MODULATE 12
#define OP_INPUT_OPAQUE 13
#define OP_MUL_ALPHA 14
#define OP_PERLIN_NOISE 15
#endif
