// The one-dimensional Gaussian blur as one function: the tap loop, the packed half-kernel lookup,
// the draw-level subset clamp and a shader-tiled source's per-tap mapping all live here, so every
// value a caller supplies is a name the bytecode routes can record. The linked modules read no
// uniform block (the body owns it and its members are per-draw) and no array crosses a module
// boundary, so the kernel slots arrive as named values. The slot and lane are picked with chains
// of compile-time indices rather than indexing abs(tap) directly: SwiftShader miscompiles a
// dynamic index into a std140 block array and silently reads zeros.
//@function gaussianBlur1D
//@op GaussianBlur1D
//@frag
vec4 gaussianBlur1D(sampler2D sourceSampler, highp vec2 baseCoord /*@in*/, highp vec2 tapStep,
                    int tapRadius, highp vec4 sourceSubset /*@in*/,
                    int subsetEnabled /*@in*/, int tiledSource /*@in*/,
                    int tiledModeX /*@in*/, int tiledModeY /*@in*/,
                    highp vec4 tiledSubset /*@in*/, highp vec4 tiledClamp /*@in*/,
                    highp vec2 tiledDimension /*@in*/, int seamBlend /*@in*/,
                    int borderPass /*@in*/, vec4 kernelSlot0, vec4 kernelSlot1, vec4 kernelSlot2, vec4 kernelSlot3, vec4 kernelSlot4, vec4 kernelSlot5, vec4 kernelSlot6, vec4 kernelSlot7, vec4 kernelSlot8, vec4 kernelSlot9, vec4 kernelSlot10, vec4 kernelSlot11, vec4 kernelSlot12, vec4 kernelSlot13, vec4 kernelSlot14, vec4 kernelSlot15, vec4 kernelSlot16) {
  vec4 sum = vec4(0.0);
  for (int step = 0; step <= 40; ++step) {
    int tap = step - tapRadius;
    if (tap > tapRadius) {
      break;
    }
    int kernelOffset = abs(tap);
    float weight = tgfxKernelWeight(kernelSlot0, kernelSlot1, kernelSlot2, kernelSlot3,
                                    kernelSlot4, kernelSlot5, kernelSlot6, kernelSlot7,
                                    kernelSlot8, kernelSlot9, kernelSlot10, kernelSlot11,
                                    kernelSlot12, kernelSlot13, kernelSlot14, kernelSlot15,
                                    kernelSlot16, kernelOffset);

    vec2 sampleCoord = baseCoord + tapStep * float(tap);
    vec4 tapColor = vec4(0.0);
    if (tiledSource != 0) {
      vec2 tapIn;
      vec2 tapSubset;
      vec2 tapClamped;
      vec2 mappedCoord = tiledMapCoord(vec3(sampleCoord, 1.0), false, tiledModeX, tiledModeY,
                                       tiledSubset, tiledClamp, tiledDimension, tapIn, tapSubset,
                                       tapClamped);
      tapColor = texture(sourceSampler, mappedCoord);
      if (seamBlend != 0) {
        tapColor = tiledSeamBlend(sourceSampler, tapColor, tiledModeX, tiledModeY, tiledClamp,
                                  tiledDimension, tapIn, tapSubset, tapClamped, 0);
      }
      if (borderPass != 0) {
        tapColor = tiledApplyBorder(tapColor, tiledModeX, tiledModeY, tiledSubset, tapIn, tapSubset,
                                    tapClamped);
      }
    } else {
      if (subsetEnabled != 0) {
        sampleCoord = clamp(sampleCoord, sourceSubset.xy, sourceSubset.zw);
      }
      tapColor = texture(sourceSampler, sampleCoord);
    }
    sum += tapColor * weight;
    if (tap == tapRadius) {
      break;
    }
  }
  return sum;
}
