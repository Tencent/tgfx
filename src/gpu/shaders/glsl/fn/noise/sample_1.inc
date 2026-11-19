//@function noiseSample1
vec4 noiseSample1(vec3 sourceCoords, vec4 frequencyStitch, ivec4 modeOctaves) {
  highp vec2 coord = sourceCoords.xy / sourceCoords.z;
  return aotPerlinNoise(coord, frequencyStitch.xy, frequencyStitch.zw, modeOctaves.x, modeOctaves.y,
                        modeOctaves.z, TextureSampler_1, TextureSampler_2);
}
