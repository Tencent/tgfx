// gaussian blur body
#define MAX_SIGMA 9
#define ShaderModeX GaussianBlur_ShaderModeX
#define ShaderModeY GaussianBlur_ShaderModeY
#define Subset GaussianBlur_Subset
#define Clamp GaussianBlur_Clamp
#define Dimension GaussianBlur_Dimension
#include "source/tiled_sample.inc"
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension

void GaussianBlur_fragment(inout Fragment f) {
  vec2 offset = GaussianBlur_Step;
  int radius = GaussianBlur_Radius;

  vec4 sum = vec4(0.0);

  for (int j = 0; j <= 4 * (MAX_SIGMA + 1); ++j) {
    int i = j - radius;
    if (i > radius) {
      break;
    }
    // Read the half-kernel weight for the current loop offset. The slot and the lane inside it
    // are picked with chains of compile-time indices rather than indexing the uniform array with
    // abs(i) directly: SwiftShader miscompiles a dynamic index into a std140 uniform block array
    // and silently reads zeros, which drops the blur (same workaround as the runtime emission in
    // GLSLGaussianBlur1DFragmentProcessor::AppendKernelWeight).
    int kernelOffset = abs(i);
    int kernelSlot = kernelOffset / 4;
    vec4 kernelValues = GaussianBlur_Kernel[0];
    if (kernelSlot == 1) { kernelValues = GaussianBlur_Kernel[1]; }
    if (kernelSlot == 2) { kernelValues = GaussianBlur_Kernel[2]; }
    if (kernelSlot == 3) { kernelValues = GaussianBlur_Kernel[3]; }
    if (kernelSlot == 4) { kernelValues = GaussianBlur_Kernel[4]; }
    if (kernelSlot == 5) { kernelValues = GaussianBlur_Kernel[5]; }
    if (kernelSlot == 6) { kernelValues = GaussianBlur_Kernel[6]; }
    if (kernelSlot == 7) { kernelValues = GaussianBlur_Kernel[7]; }
    if (kernelSlot == 8) { kernelValues = GaussianBlur_Kernel[8]; }
    if (kernelSlot == 9) { kernelValues = GaussianBlur_Kernel[9]; }
    if (kernelSlot == 10) { kernelValues = GaussianBlur_Kernel[10]; }
    if (kernelSlot == 11) { kernelValues = GaussianBlur_Kernel[11]; }
    if (kernelSlot == 12) { kernelValues = GaussianBlur_Kernel[12]; }
    if (kernelSlot == 13) { kernelValues = GaussianBlur_Kernel[13]; }
    if (kernelSlot == 14) { kernelValues = GaussianBlur_Kernel[14]; }
    if (kernelSlot == 15) { kernelValues = GaussianBlur_Kernel[15]; }
    if (kernelSlot == 16) { kernelValues = GaussianBlur_Kernel[16]; }
    int kernelLane = kernelOffset - kernelSlot * 4;
    float weight = kernelValues.x;
    if (kernelLane == 1) { weight = kernelValues.y; }
    if (kernelLane == 2) { weight = kernelValues.z; }
    if (kernelLane == 3) { weight = kernelValues.w; }

    vec2 sampleCoord = BlurSourceGeometry_vCoord + offset * float(i);
    vec4 texColor = vec4(0.0);
    if (GaussianBlur_TiledChild != 0) {
    // The child is a TiledTextureEffect: tile each tap exactly as the runtime does per sample.
    vec2 tapInCoord;
    vec2 tapSubsetCoord;
    vec2 tapClampedCoord;
    vec2 tapCoord = tiledMapCoord(sampleCoord, false, tapInCoord, tapSubsetCoord, tapClampedCoord);
    texColor = texture(GaussianBlur_Sampler, tapCoord);
    // RepeatLinearNone(3) seam blend, ported from the runtime GLSLTiledTextureEffect emission: a
    // wrapped coordinate that clamps means the linear footprint crosses the subset edge, so blend
    // with a sample at the opposite clamp edge (diagonal read when both axes clamp). The signed
    // single-axis weights intentionally match the runtime. Mode 3 uses pixel-space coordinates,
    // so the repeat reads scale by Dimension like tiledMapCoord's returned sample coord does.
    bool repeatX = GaussianBlur_ShaderModeX == 3 && tapSubsetCoord.x != tapClampedCoord.x;
    bool repeatY = GaussianBlur_ShaderModeY == 3 && tapSubsetCoord.y != tapClampedCoord.y;
    if (repeatX || repeatY) {
      float errX = tapSubsetCoord.x - tapClampedCoord.x;
      float errY = tapSubsetCoord.y - tapClampedCoord.y;
      float repeatCoordX = errX > 0.0 ? GaussianBlur_Clamp.x : GaussianBlur_Clamp.z;
      float repeatCoordY = errY > 0.0 ? GaussianBlur_Clamp.y : GaussianBlur_Clamp.w;
      if (repeatX && repeatY) {
        vec4 repeatReadX =
            texture(GaussianBlur_Sampler, vec2(repeatCoordX, tapClampedCoord.y) * GaussianBlur_Dimension);
        vec4 repeatReadY =
            texture(GaussianBlur_Sampler, vec2(tapClampedCoord.x, repeatCoordY) * GaussianBlur_Dimension);
        vec4 repeatReadXY = texture(GaussianBlur_Sampler, vec2(repeatCoordX, repeatCoordY) * GaussianBlur_Dimension);
        texColor = mix(mix(texColor, repeatReadX, abs(errX)),
                       mix(repeatReadY, repeatReadXY, abs(errX)), abs(errY));
      } else if (repeatX) {
        vec4 repeatReadX =
            texture(GaussianBlur_Sampler, vec2(repeatCoordX, tapClampedCoord.y) * GaussianBlur_Dimension);
        texColor = mix(texColor, repeatReadX, errX);
      } else {
        vec4 repeatReadY =
            texture(GaussianBlur_Sampler, vec2(tapClampedCoord.x, repeatCoordY) * GaussianBlur_Dimension);
        texColor = mix(texColor, repeatReadY, errY);
      }
    }
    texColor = tiledApplyBorder(texColor, tapInCoord, tapSubsetCoord, tapClampedCoord);
    } else {
      sampleCoord = clamp(sampleCoord, GaussianBlur_Subset.xy, GaussianBlur_Subset.zw);
      texColor = texture(GaussianBlur_Sampler, sampleCoord);
    }
    if (GaussianBlur_AlphaChild != 0) {
      // The runtime child readback samples with Swizzle::ForRead(ALPHA_8) = .rrrr, so an
      // alpha-only tap carries the mask value in every channel; the raw (r,0,0,1) readback
      // would leave the alpha channel constant 1.
      texColor = texColor.rrrr;
    }
    sum += texColor * weight;

    if (i == radius) {
      break;
    }
  }

  f.color = sum;
}
