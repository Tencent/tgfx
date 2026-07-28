// Shared pointwise-operator uniform declarations. Included inside a FragmentUniformBlock by any
// kernel that applies exactly one runtime-selected pointwise operator (TexturedEffectShader,
// PerlinNoiseFillShader). OpType selects the operator; the remaining fields are the per-operator
// parameters. See pointwise_op.inc for the matching logic and OpType encoding.
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
