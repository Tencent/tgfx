// The op names of the pointwise interpreters (chain and slot). The values are injected as defines
// from the Op table in KernelContract.h by shader_build_tool, so this file only documents the
// include point; no GLSL file may define an OP_* macro. The slot interpreter dispatches on the subset it supports
// (AOTPointwiseOpType), but every consumer includes the whole table so one op has one value
// everywhere.
#ifndef TGFX_OP_CODES_INC
#define TGFX_OP_CODES_INC
#endif
