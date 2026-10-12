#ifndef TGFX_PERLIN_NOISE_MATH_INC
#define TGFX_PERLIN_NOISE_MATH_INC

vec4 aotPerlinNoise(vec2 coord, vec2 frequency, vec2 stitchSize, int noiseType,
                    int numOctaves, int stitchTiles) {
  highp vec2 noiseVec = coord * frequency + vec2(0.0078125);
  vec4 color = vec4(0.0);
  highp vec2 stitch = stitchSize;
  float ratio = 1.0;
  for (int octave = 0; octave < numOctaves; ++octave) {
    highp vec4 floorVal;
    floorVal.xy = floor(noiseVec);
    floorVal.zw = floorVal.xy + vec2(1.0);
    highp vec2 fractVal = fract(noiseVec);
    vec2 noiseSmooth = smoothstep(0.0, 1.0, fractVal);
    if (stitchTiles != 0) {
      floorVal -= step(stitch.xyxy, floorVal) * stitch.xyxy;
    }
    floorVal = mod(floorVal, 256.0);
    highp float permX = aotPerlinPermutation(vec2((floorVal.x + 0.5) / 256.0, 0.5)).r;
    highp float permZ = aotPerlinPermutation(vec2((floorVal.z + 0.5) / 256.0, 0.5)).r;
    highp vec2 latticeIdx = floor(vec2(permX, permZ) * 255.0 + 0.5);
    highp vec4 bcoords = mod(latticeIdx.xyxy + floorVal.yyww, 256.0);
    vec4 noiseResult;
    float chanCoords[4] = float[4](0.125, 0.375, 0.625, 0.875);
    for (int ch = 0; ch < 4; ++ch) {
      vec4 lattice = aotPerlinGradient(vec2((bcoords.x + 0.5) / 256.0, chanCoords[ch]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = aotPerlinGradient(vec2((bcoords.y + 0.5) / 256.0, chanCoords[ch]));
      float v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
                    fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = aotPerlinGradient(vec2((bcoords.w + 0.5) / 256.0, chanCoords[ch]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(1.0, 1.0));
      lattice = aotPerlinGradient(vec2((bcoords.z + 0.5) / 256.0, chanCoords[ch]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0),
              fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult[ch] = mix(a, b, noiseSmooth.y);
    }
    if (noiseType != 0) {
      color += abs(noiseResult) * ratio;
    } else {
      color += noiseResult * ratio;
    }
    noiseVec *= 2.0;
    ratio *= 0.5;
    stitch *= 2.0;
  }
  if (noiseType == 0) {
    color = color * 0.5 + 0.5;
  }
  color = clamp(color, 0.0, 1.0);
  return vec4(color.rgb * color.aaa, color.a);
}

#endif
