// The one-dimensional Gaussian blur as one function: the tap loop, the packed half-kernel lookup,
// the draw-level subset clamp and a shader-tiled source's per-tap mapping all live here, so every
// value a caller supplies is a name the bytecode routes can record. The linked modules read no
// uniform block (the body owns it and its members are per-draw) and no array crosses a module
// boundary, so the kernel slots arrive as named values. The slot and lane are picked with chains
// of compile-time indices rather than indexing abs(tap) directly: SwiftShader miscompiles a
// dynamic index into a std140 block array and silently reads zeros.
//@function gaussianBlur1D
vec4 gaussianBlur1D(sampler2D sourceSampler, highp vec2 baseCoord, highp vec2 tapStep, int tapRadius,
                    highp vec4 sourceSubset, int subsetEnabled, int tiledSource, int tiledModeX,
                    int tiledModeY, highp vec4 tiledSubset, highp vec4 tiledClamp,
                    highp vec2 tiledDimension, int seamBlend, int borderPass, vec4 kernelSlot0, vec4 kernelSlot1, vec4 kernelSlot2, vec4 kernelSlot3, vec4 kernelSlot4, vec4 kernelSlot5, vec4 kernelSlot6, vec4 kernelSlot7, vec4 kernelSlot8, vec4 kernelSlot9, vec4 kernelSlot10, vec4 kernelSlot11, vec4 kernelSlot12, vec4 kernelSlot13, vec4 kernelSlot14, vec4 kernelSlot15, vec4 kernelSlot16) {
  vec4 sum = vec4(0.0);
  for (int step = 0; step <= 40; ++step) {
    int tap = step - tapRadius;
    if (tap > tapRadius) {
      break;
    }
    int kernelOffset = abs(tap);
    int kernelSlot = kernelOffset / 4;
    vec4 kernelValues = kernelSlot0;
    if (kernelSlot == 0) { kernelValues = kernelSlot0; }
    if (kernelSlot == 1) { kernelValues = kernelSlot1; }
    if (kernelSlot == 2) { kernelValues = kernelSlot2; }
    if (kernelSlot == 3) { kernelValues = kernelSlot3; }
    if (kernelSlot == 4) { kernelValues = kernelSlot4; }
    if (kernelSlot == 5) { kernelValues = kernelSlot5; }
    if (kernelSlot == 6) { kernelValues = kernelSlot6; }
    if (kernelSlot == 7) { kernelValues = kernelSlot7; }
    if (kernelSlot == 8) { kernelValues = kernelSlot8; }
    if (kernelSlot == 9) { kernelValues = kernelSlot9; }
    if (kernelSlot == 10) { kernelValues = kernelSlot10; }
    if (kernelSlot == 11) { kernelValues = kernelSlot11; }
    if (kernelSlot == 12) { kernelValues = kernelSlot12; }
    if (kernelSlot == 13) { kernelValues = kernelSlot13; }
    if (kernelSlot == 14) { kernelValues = kernelSlot14; }
    if (kernelSlot == 15) { kernelValues = kernelSlot15; }
    if (kernelSlot == 16) { kernelValues = kernelSlot16; }
    int kernelLane = kernelOffset - kernelSlot * 4;
    float weight = kernelValues.x;
    if (kernelLane == 1) { weight = kernelValues.y; }
    if (kernelLane == 2) { weight = kernelValues.z; }
    if (kernelLane == 3) { weight = kernelValues.w; }

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
