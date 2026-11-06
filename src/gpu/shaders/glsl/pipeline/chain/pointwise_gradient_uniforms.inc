// The gradient module's runtime selectors (OP_GRADIENT), included inside the FragmentUniformBlock
// by the kernel entry. The gradient's data block (borders, stops, scale/bias pairs, thresholds,
// the coordinate matrix) lives in the chain data pool's chain-wide region, whose layout exists
// only in contract/ChainOps.h; these three ints choose the code path and stay runtime uniforms
// until the structural fields move to specialization constants.
  int GradientLayoutType;
  int GradientColorizerKind;
  int GradientIntervalCount;
  // Leaf index of the LUT gradient texture (a sampler-only child with no DAG slot), read by the
  // OP_GRADIENT LUT colorizer branch. -1 when no LUT gradient is present.
  int GradientLUTLeaf;
