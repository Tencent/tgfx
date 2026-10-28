// Fields of the gradient module (pointwise_gradient.inc), included inside the FragmentUniformBlock
// by the kernel entry.
  // Gradient source block (OP_GRADIENT): at most one gradient slot per extended chain. All fields are
  // runtime uniforms written only when a gradient slot is present; names mirror the dedicated
  // gradient kernels with a "Gradient" prefix.
  int GradientLayoutType;
  float GradientBias;
  float GradientScale;
  int GradientColorizerKind;
  vec4 GradientLeftBorder;
  vec4 GradientRightBorder;
  vec4 GradientStart;
  vec4 GradientEnd;
  vec4 GradientScale01;
  vec4 GradientBias01;
  vec4 GradientScale23;
  vec4 GradientBias23;
  float GradientThreshold;
  int GradientIntervalCount;
  vec4 GradientThresholds1_7;
  vec4 GradientThresholds9_13;
  vec4 GradientScale0_1;
  vec4 GradientScale2_3;
  vec4 GradientScale4_5;
  vec4 GradientScale6_7;
  vec4 GradientScale8_9;
  vec4 GradientScale10_11;
  vec4 GradientScale12_13;
  vec4 GradientScale14_15;
  vec4 GradientBias0_1;
  vec4 GradientBias2_3;
  vec4 GradientBias4_5;
  vec4 GradientBias6_7;
  vec4 GradientBias8_9;
  vec4 GradientBias10_11;
  vec4 GradientBias12_13;
  vec4 GradientBias14_15;
  // Leaf index of the LUT gradient texture (a sampler-only child with no DAG slot), read by the
  // OP_GRADIENT LUT colorizer branch. -1 when no LUT gradient is present.
  int GradientLUTLeaf;
