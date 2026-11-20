//@function evalNoiseSlot Noise
vec4 evalNoiseSlot(vec3 sourceCoords /*@in*/, vec4 frequencyStitch, ivec4 modeOctaves,
                   sampler2D s0, sampler2D s1, sampler2D s2, sampler2D s3) {
  if (modeOctaves.w == 0) {
    return noiseSample0(sourceCoords, frequencyStitch, modeOctaves, s0, s1);
  }
  if (modeOctaves.w == 1) {
    return noiseSample1(sourceCoords, frequencyStitch, modeOctaves, s1, s2);
  }
  return noiseSample2(sourceCoords, frequencyStitch, modeOctaves, s2, s3);
}
