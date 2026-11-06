// Fields of the Perlin noise module (pointwise_noise.inc), included inside the
// FragmentUniformBlock by the kernel entry.
  // The noise block (frequency and stitch rect) lives in the chain data pool; these selectors
  // stay runtime uniforms until the structural fields move to specialization constants.
  ivec4 NoiseModeOctaves;
