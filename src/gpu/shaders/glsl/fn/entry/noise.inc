//@function evalNoiseSlot Noise
vec4 evalNoiseSlot(vec3 sourceCoords /*@in*/, vec4 frequencyStitch, ivec4 modeOctaves) {
  if (modeOctaves.w == 0) {
    return noiseSample0(sourceCoords, frequencyStitch, modeOctaves);
  }
  if (modeOctaves.w == 1) {
    return noiseSample1(sourceCoords, frequencyStitch, modeOctaves);
  }
  return noiseSample2(sourceCoords, frequencyStitch, modeOctaves);
}
