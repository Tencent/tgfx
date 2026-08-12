// Shared pointwise-operator uniform declarations. Included inside a FragmentUniformBlock by any
// kernel that applies a runtime-selected pointwise operator (TexturedEffectShader,
// PerlinNoiseFillShader, PointwiseTailShader). OpType selects the operator; the remaining fields are
// the per-operator parameters. See pointwise_op.inc for the matching logic and OpType encoding.
//
// Single-slot kernels include this directly and get the base field names. Multi-slot kernels include
// pointwise_slot_bind.inc first to redirect every name below to a slot-prefixed field (Slot0OpType,
// Slot1OpType, ...), which keeps each slot's parameters independent. The reflection extractor reads
// flat top-level block members (ReflectionExtractor.cpp), so slots must be distinct flat fields
// rather than a nested struct.
//
// Field order must stay in sync with AOTPointwiseTailProcessor's uniform writes: the std140 block
// layout is derived from this order, so reordering changes the binary layout of every including
// kernel.
  int OpType;
  // ColorMatrix op
  mat4 ColorMatrix;
  vec4 ColorVector;
  // Luma op
  float Kr;
  float Kg;
  float Kb;
  // AlphaThreshold op
  float Threshold;
  // ColorSpaceXform op
  int CSFlags;
  vec4 SrcTF0;
  vec4 SrcTF1;
  int SrcTFType;
  vec4 SrcOOTF;
  mat3 ColorXform;
  vec4 DstOOTF;
  vec4 DstTF0;
  vec4 DstTF1;
  int DstTFType;
  // ConstColor op
  vec4 ConstColorValue;
  int ConstInputMode;
  // Blend op (one operand is ConstColorValue; BlendConstFirst picks the operand order)
  int BlendModeValue;
  int BlendConstFirst;
