//@function noiseSample0
vec4 noiseSample0(vec3 sourceCoords, vec4 frequencyStitch, ivec4 modeOctaves) {
  highp vec2 coord = sourceCoords.xy / sourceCoords.z;
  return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, modeOctaves.x, modeOctaves.y,
                        modeOctaves.z, TextureSampler_0, TextureSampler_1);
}
