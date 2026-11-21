// The one-dimensional Gaussian blur as one chain function: the packed half-kernel rides the
// BlurKernel uniform array, the radius and step are runtime values, so one text serves every
// sigma within the family's [1, 10] acceptance range. The kernel table's slot and lane are picked
// with chains of compile-time indices rather than indexing the uniform array with abs(i)
// directly: SwiftShader miscompiles a dynamic index into a std140 uniform block array and
// silently reads zeros (the same workaround as the runtime emission). The child leaf's sampler
// and transformed coordinate arrive as arguments; a shader-tiled child maps every tap through
// the shared tiled catalog functions.
//@function gaussianBlur1D
vec4 gaussianBlur1D(sampler2D tex, vec3 coord) {
  vec2 baseCoord = coord.xy / coord.z;
  vec2 offset = BlurStep;
  int radius = BlurRadius;
  vec4 sum = vec4(0.0);
  for (int j = 0; j <= 40; ++j) {
    int i = j - radius;
    if (i > radius) {
      break;
    }
    int kernelOffset = abs(i);
    int kernelSlot = kernelOffset / 4;
    vec4 kernelValues = BlurKernel[0];
    if (kernelSlot == 1) { kernelValues = BlurKernel[1]; }
    if (kernelSlot == 2) { kernelValues = BlurKernel[2]; }
    if (kernelSlot == 3) { kernelValues = BlurKernel[3]; }
    if (kernelSlot == 4) { kernelValues = BlurKernel[4]; }
    if (kernelSlot == 5) { kernelValues = BlurKernel[5]; }
    if (kernelSlot == 6) { kernelValues = BlurKernel[6]; }
    if (kernelSlot == 7) { kernelValues = BlurKernel[7]; }
    if (kernelSlot == 8) { kernelValues = BlurKernel[8]; }
    if (kernelSlot == 9) { kernelValues = BlurKernel[9]; }
    if (kernelSlot == 10) { kernelValues = BlurKernel[10]; }
    if (kernelSlot == 11) { kernelValues = BlurKernel[11]; }
    if (kernelSlot == 12) { kernelValues = BlurKernel[12]; }
    if (kernelSlot == 13) { kernelValues = BlurKernel[13]; }
    if (kernelSlot == 14) { kernelValues = BlurKernel[14]; }
    if (kernelSlot == 15) { kernelValues = BlurKernel[15]; }
    if (kernelSlot == 16) { kernelValues = BlurKernel[16]; }
    int kernelLane = kernelOffset - kernelSlot * 4;
    float weight = kernelValues.x;
    if (kernelLane == 1) { weight = kernelValues.y; }
    if (kernelLane == 2) { weight = kernelValues.z; }
    if (kernelLane == 3) { weight = kernelValues.w; }

    vec2 sampleCoord = baseCoord + offset * float(i);
    vec4 texColor = vec4(0.0);
    if (BlurTiledChild != 0) {
      vec2 tapIn;
      vec2 tapSubset;
      vec2 tapClamped;
      vec2 tapCoord = tiledMapCoord(vec3(sampleCoord, 1.0), false, BlurTiledModeX,
                                    BlurTiledModeY, BlurTiledSubset, BlurTiledClamp,
                                    BlurTiledDimension, tapIn, tapSubset, tapClamped);
      texColor = texture(tex, tapCoord);
      texColor = tiledSeamBlend(tex, texColor, BlurTiledModeX, BlurTiledModeY, BlurTiledClamp,
                                BlurTiledDimension, tapIn, tapSubset, tapClamped, 0);
      texColor = tiledApplyBorder(texColor, BlurTiledModeX, BlurTiledModeY, BlurTiledSubset,
                                  tapIn, tapSubset, tapClamped);
    } else {
      sampleCoord = clamp(sampleCoord, BlurSubset.xy, BlurSubset.zw);
      texColor = texture(tex, sampleCoord);
    }
    sum += texColor * weight;
    if (i == radius) {
      break;
    }
  }
  return sum;
}
