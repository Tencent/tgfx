// The Perlin noise kernel: the octave loop, the permutation and lattice reads, and the
// per-channel corner interpolation. The per-octave math is the runtime emission's, moved here
// whole so the IR route can name it as one call. The loop bound stays a constant (GLSL ES 1.0's
// constant-loop constraint) and the runtime octave count exits through the break; the channel
// corners are spelled out per channel because the ES index rules keep vector subscripts constant.
//@function tgxxPerlinNoise
//@op PerlinNoiseKernel
//@frag
vec4 tgxxPerlinNoise(sampler2D permSampler, sampler2D noiseSampler, highp vec2 texCoord /*@in*/,
                     vec2 baseFrequency, vec2 stitchData, int numOctaves /*@in*/,
                     int noiseType /*@in*/, int stitchTiles /*@in*/) {
  highp vec2 noiseVec = tgfxNoiseCoord(texCoord, baseFrequency);
  vec4 color = vec4(0.0);
  float ratio = 1.0;
  for (int octave = 0; octave < 8; ++octave) {
    if (octave >= numOctaves) {
      break;
    }
    highp vec4 floorVal;
    floorVal.xy = floor(noiseVec);
    floorVal.zw = floorVal.xy + vec2(1.0);
    highp vec2 fractVal = fract(noiseVec);
    vec2 noiseSmooth = smoothstep(0.0, 1.0, fractVal);
    if (stitchTiles != 0) {
      floorVal -= step(stitchData.xyxy, floorVal) * stitchData.xyxy;
    }
    floorVal = mod(floorVal, 256.0);
    highp float permX = texture(permSampler, vec2((floorVal.x + 0.5) / 256.0, 0.5)).r;
    highp float permZ = texture(permSampler, vec2((floorVal.z + 0.5) / 256.0, 0.5)).r;
    highp vec2 latticeIdx = tgfxNoiseLatticeIdx(vec2(permX, permZ));
    highp vec4 bcoords = tgfxNoiseBcoords(latticeIdx, floorVal);

    vec4 noiseResult;
    // Channel 0 (row y = 0.125)
    {
      vec4 lattice = texture(noiseSampler, vec2((bcoords.x + 0.5) / 256.0, 0.125));
      float u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 0.0));
      lattice = texture(noiseSampler, vec2((bcoords.y + 0.5) / 256.0, 0.125));
      float v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(noiseSampler, vec2((bcoords.w + 0.5) / 256.0, 0.125));
      v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 1.0));
      lattice = texture(noiseSampler, vec2((bcoords.z + 0.5) / 256.0, 0.125));
      u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.x = mix(a, b, noiseSmooth.y);
    }
    // Channel 1 (row y = 0.375)
    {
      vec4 lattice = texture(noiseSampler, vec2((bcoords.x + 0.5) / 256.0, 0.375));
      float u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 0.0));
      lattice = texture(noiseSampler, vec2((bcoords.y + 0.5) / 256.0, 0.375));
      float v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(noiseSampler, vec2((bcoords.w + 0.5) / 256.0, 0.375));
      v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 1.0));
      lattice = texture(noiseSampler, vec2((bcoords.z + 0.5) / 256.0, 0.375));
      u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.y = mix(a, b, noiseSmooth.y);
    }
    // Channel 2 (row y = 0.625)
    {
      vec4 lattice = texture(noiseSampler, vec2((bcoords.x + 0.5) / 256.0, 0.625));
      float u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 0.0));
      lattice = texture(noiseSampler, vec2((bcoords.y + 0.5) / 256.0, 0.625));
      float v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(noiseSampler, vec2((bcoords.w + 0.5) / 256.0, 0.625));
      v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 1.0));
      lattice = texture(noiseSampler, vec2((bcoords.z + 0.5) / 256.0, 0.625));
      u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.z = mix(a, b, noiseSmooth.y);
    }
    // Channel 3 (row y = 0.875)
    {
      vec4 lattice = texture(noiseSampler, vec2((bcoords.x + 0.5) / 256.0, 0.875));
      float u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 0.0));
      lattice = texture(noiseSampler, vec2((bcoords.y + 0.5) / 256.0, 0.875));
      float v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(noiseSampler, vec2((bcoords.w + 0.5) / 256.0, 0.875));
      v = tgfxNoiseGradDot(lattice, fractVal, vec2(1.0, 1.0));
      lattice = texture(noiseSampler, vec2((bcoords.z + 0.5) / 256.0, 0.875));
      u = tgfxNoiseGradDot(lattice, fractVal, vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult.w = mix(a, b, noiseSmooth.y);
    }

    if (noiseType == 0) {
      color += noiseResult * ratio;
    } else {
      color += abs(noiseResult) * ratio;
    }
    noiseVec *= 2.0;
    ratio *= 0.5;
    if (stitchTiles != 0) {
      stitchData *= 2.0;
    }
  }
  if (noiseType == 0) {
    color = color * 0.5 + 0.5;
  }
  return tgfxNoisePremul(color);
}
