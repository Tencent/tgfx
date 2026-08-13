// Shared pointwise-operator uniform declarations. Included inside a FragmentUniformBlock by any
// kernel that applies a runtime-selected pointwise operator (TexturedEffectShader,
// PerlinNoiseFillShader, PointwiseTailShader). OpType selects the operator; the remaining fields are
// the per-operator parameters. See pointwise_op.inc for the matching logic and OpType encoding.
//
// Single-slot kernels include this with TGFX_SLOT_ARRAY_SUFFIX undefined (scalar fields).
// Multi-slot kernels define TGFX_SLOT_ARRAY_SUFFIX to a bracketed slot capacity (e.g. [2]) before
// including, turning every field into a per-slot array indexed by the slot parameter of
// applyPointwiseOp. The reflection extractor reads flat top-level block members
// (ReflectionExtractor.cpp), so slots must be flat fields or arrays rather than a nested struct.
//
// Field order must stay in sync with AOTPointwiseTailProcessor's uniform writes: the std140 block
// layout is derived from this order, so reordering changes the binary layout of every including
// kernel.
#ifndef TGFX_SLOT_ARRAY_SUFFIX
#define TGFX_SLOT_ARRAY_SUFFIX
#endif
  int OpType TGFX_SLOT_ARRAY_SUFFIX;
  // ColorMatrix op
  mat4 ColorMatrix TGFX_SLOT_ARRAY_SUFFIX;
  vec4 ColorVector TGFX_SLOT_ARRAY_SUFFIX;
  // Luma op
  float Kr TGFX_SLOT_ARRAY_SUFFIX;
  float Kg TGFX_SLOT_ARRAY_SUFFIX;
  float Kb TGFX_SLOT_ARRAY_SUFFIX;
  // AlphaThreshold op
  float Threshold TGFX_SLOT_ARRAY_SUFFIX;
  // ColorSpaceXform op
  int CSFlags TGFX_SLOT_ARRAY_SUFFIX;
  vec4 SrcTF0 TGFX_SLOT_ARRAY_SUFFIX;
  vec4 SrcTF1 TGFX_SLOT_ARRAY_SUFFIX;
  int SrcTFType TGFX_SLOT_ARRAY_SUFFIX;
  vec4 SrcOOTF TGFX_SLOT_ARRAY_SUFFIX;
  mat3 ColorXform TGFX_SLOT_ARRAY_SUFFIX;
  vec4 DstOOTF TGFX_SLOT_ARRAY_SUFFIX;
  vec4 DstTF0 TGFX_SLOT_ARRAY_SUFFIX;
  vec4 DstTF1 TGFX_SLOT_ARRAY_SUFFIX;
  int DstTFType TGFX_SLOT_ARRAY_SUFFIX;
  // ConstColor op
  vec4 ConstColorValue TGFX_SLOT_ARRAY_SUFFIX;
  int ConstInputMode TGFX_SLOT_ARRAY_SUFFIX;
  // Blend op (one operand is ConstColorValue; BlendConstFirst picks the operand order)
  int BlendModeValue TGFX_SLOT_ARRAY_SUFFIX;
  int BlendConstFirst TGFX_SLOT_ARRAY_SUFFIX;
