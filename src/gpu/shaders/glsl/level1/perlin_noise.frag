// PerlinNoiseFillShader fragment shader
// Ported from GLSLPerlinNoiseFragmentProcessor::emitCode. The runtime path bakes noiseType, octave
// count and tile stitching into generated code; here they are runtime uniforms (NoiseType,
// NumOctaves, StitchTiles) so one shader covers all of them:
//   - the octave loop runs a uniform number of iterations (NumOctaves, clamped [1,255]);
//   - NoiseType (0=FractalNoise, 1=Turbulence) and StitchTiles are uniform conditionals.
// Two per-draw lookup textures are bound as samplers: TextureSampler_0 = permutations (256x1 A8,
// ForRead swizzle RRRR so the single stored channel reads through .r) and TextureSampler_1 = noise
// (256x4 RGBA8888, identity swizzle). Output is premultiplied RGBA.
// Two pointwise-operator slots follow the noise (same runtime-uniform design as
// PointwiseTailShader): the base slot is shared with an operator FP composed on top of the perlin
// processor (OpType == OP_NONE when there is none), while the Slot1 record is written only by the
// processor's own slot storage, letting one kernel evaluate Perlin -> op -> op in a single pass.
// Permutation dimensions (frag): HAS_XP (int, 3), HAS_COVERAGE (bool).
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif

#define NOISE_FRACTAL 0

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec2 baseFrequency;
  vec2 stitchData;
  int NoiseType;
  int NumOctaves;
  int StitchTiles;
#include "pointwise_op_uniforms.inc"

#define TGFX_SLOT_OP_TYPE Slot1OpType
#define TGFX_SLOT_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot1ColorVector
#define TGFX_SLOT_KR Slot1Kr
#define TGFX_SLOT_KG Slot1Kg
#define TGFX_SLOT_KB Slot1Kb
#define TGFX_SLOT_THRESHOLD Slot1Threshold
#define TGFX_SLOT_CS_FLAGS Slot1CSFlags
#define TGFX_SLOT_SRC_TF0 Slot1SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot1SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot1SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot1SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot1ColorXform
#define TGFX_SLOT_DST_OOTF Slot1DstOOTF
#define TGFX_SLOT_DST_TF0 Slot1DstTF0
#define TGFX_SLOT_DST_TF1 Slot1DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot1DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot1ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot1ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot1BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot1BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC perlinSlotSrcTF1
#define TGFX_SLOT_DST_TF_FUNC perlinSlotDstTF1
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot1
#include "pointwise_slot_bind.inc"
#include "pointwise_op_uniforms.inc"
#include "pointwise_slot_unbind.inc"

#define TGFX_SLOT_OP_TYPE Slot2OpType
#define TGFX_SLOT_COLOR_MATRIX Slot2ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot2ColorVector
#define TGFX_SLOT_KR Slot2Kr
#define TGFX_SLOT_KG Slot2Kg
#define TGFX_SLOT_KB Slot2Kb
#define TGFX_SLOT_THRESHOLD Slot2Threshold
#define TGFX_SLOT_CS_FLAGS Slot2CSFlags
#define TGFX_SLOT_SRC_TF0 Slot2SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot2SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot2SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot2SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot2ColorXform
#define TGFX_SLOT_DST_OOTF Slot2DstOOTF
#define TGFX_SLOT_DST_TF0 Slot2DstTF0
#define TGFX_SLOT_DST_TF1 Slot2DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot2DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot2ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot2ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot2BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot2BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC perlinSlotSrcTF2
#define TGFX_SLOT_DST_TF_FUNC perlinSlotDstTF2
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot2
#include "pointwise_slot_bind.inc"
#include "pointwise_op_uniforms.inc"
#include "pointwise_slot_unbind.inc"
#include "xp_uniforms.inc"
};

layout(location = 0) in vec2 TransformedCoords_0;
#if HAS_COVERAGE
layout(location = 1) in float vCoverage;
#endif

layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;  // permutations (A8)
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;  // noise (RGBA)

// The two noise lookup samplers occupy bindings 0 and 1, so the XferProcessor dst texture (DST_TEX
// mode) lands on binding 2.
#define XP_DST_TEX_BINDING 2
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

layout(location = 0) out vec4 fragColor;

#include "pointwise_op.inc"

#define TGFX_SLOT_OP_TYPE Slot1OpType
#define TGFX_SLOT_COLOR_MATRIX Slot1ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot1ColorVector
#define TGFX_SLOT_KR Slot1Kr
#define TGFX_SLOT_KG Slot1Kg
#define TGFX_SLOT_KB Slot1Kb
#define TGFX_SLOT_THRESHOLD Slot1Threshold
#define TGFX_SLOT_CS_FLAGS Slot1CSFlags
#define TGFX_SLOT_SRC_TF0 Slot1SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot1SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot1SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot1SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot1ColorXform
#define TGFX_SLOT_DST_OOTF Slot1DstOOTF
#define TGFX_SLOT_DST_TF0 Slot1DstTF0
#define TGFX_SLOT_DST_TF1 Slot1DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot1DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot1ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot1ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot1BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot1BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC perlinSlotSrcTF1
#define TGFX_SLOT_DST_TF_FUNC perlinSlotDstTF1
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot1
#include "pointwise_slot_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_unbind.inc"

#define TGFX_SLOT_OP_TYPE Slot2OpType
#define TGFX_SLOT_COLOR_MATRIX Slot2ColorMatrix
#define TGFX_SLOT_COLOR_VECTOR Slot2ColorVector
#define TGFX_SLOT_KR Slot2Kr
#define TGFX_SLOT_KG Slot2Kg
#define TGFX_SLOT_KB Slot2Kb
#define TGFX_SLOT_THRESHOLD Slot2Threshold
#define TGFX_SLOT_CS_FLAGS Slot2CSFlags
#define TGFX_SLOT_SRC_TF0 Slot2SrcTF0
#define TGFX_SLOT_SRC_TF1 Slot2SrcTF1
#define TGFX_SLOT_SRC_TF_TYPE Slot2SrcTFType
#define TGFX_SLOT_SRC_OOTF Slot2SrcOOTF
#define TGFX_SLOT_COLOR_XFORM Slot2ColorXform
#define TGFX_SLOT_DST_OOTF Slot2DstOOTF
#define TGFX_SLOT_DST_TF0 Slot2DstTF0
#define TGFX_SLOT_DST_TF1 Slot2DstTF1
#define TGFX_SLOT_DST_TF_TYPE Slot2DstTFType
#define TGFX_SLOT_CONST_COLOR_VALUE Slot2ConstColorValue
#define TGFX_SLOT_CONST_INPUT_MODE Slot2ConstInputMode
#define TGFX_SLOT_BLEND_MODE Slot2BlendModeValue
#define TGFX_SLOT_BLEND_CONST_FIRST Slot2BlendConstFirst
#define TGFX_SLOT_SRC_TF_FUNC perlinSlotSrcTF2
#define TGFX_SLOT_DST_TF_FUNC perlinSlotDstTF2
#define TGFX_SLOT_APPLY_FUNC applyPointwiseSlot2
#include "pointwise_slot_bind.inc"
#include "pointwise_op.inc"
#include "pointwise_slot_unbind.inc"

void main() {
  // Sub-lattice bias (1/128) keeps fract(noiseVec) away from 0 at integer baseFrequency; see the
  // runtime codegen for the derivation.
  highp vec2 noiseVec = TransformedCoords_0 * baseFrequency + vec2(0.0078125);
  vec4 color = vec4(0.0);
  highp vec2 stitch = stitchData;
  float ratio = 1.0;

  for (int octave = 0; octave < NumOctaves; ++octave) {
    highp vec4 floorVal;
    floorVal.xy = floor(noiseVec);
    floorVal.zw = floorVal.xy + vec2(1.0);
    highp vec2 fractVal = fract(noiseVec);
    vec2 noiseSmooth = smoothstep(0.0, 1.0, fractVal);

    if (StitchTiles != 0) {
      floorVal -= step(stitch.xyxy, floorVal) * stitch.xyxy;
    }
    floorVal = mod(floorVal, 256.0);

    highp float permX = texture(TextureSampler_0, vec2((floorVal.x + 0.5) / 256.0, 0.5)).r;
    highp float permZ = texture(TextureSampler_0, vec2((floorVal.z + 0.5) / 256.0, 0.5)).r;
    highp vec2 latticeIdx = floor(vec2(permX, permZ) * 255.0 + 0.5);
    highp vec4 bcoords = mod(latticeIdx.xyxy + floorVal.yyww, 256.0);

    vec4 noiseResult;
    float chanCoords[4] = float[4](0.125, 0.375, 0.625, 0.875);
    for (int ch = 0; ch < 4; ++ch) {
      vec4 lattice = texture(TextureSampler_1, vec2((bcoords.x + 0.5) / 256.0, chanCoords[ch]));
      float u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal);
      lattice = texture(TextureSampler_1, vec2((bcoords.y + 0.5) / 256.0, chanCoords[ch]));
      float v =
          dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal - vec2(1.0, 0.0));
      float a = mix(u, v, noiseSmooth.x);
      lattice = texture(TextureSampler_1, vec2((bcoords.w + 0.5) / 256.0, chanCoords[ch]));
      v = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal - vec2(1.0, 1.0));
      lattice = texture(TextureSampler_1, vec2((bcoords.z + 0.5) / 256.0, chanCoords[ch]));
      u = dot((lattice.ga + lattice.rb * 0.00390625) * 2.0 - vec2(1.0), fractVal - vec2(0.0, 1.0));
      float b = mix(u, v, noiseSmooth.x);
      noiseResult[ch] = mix(a, b, noiseSmooth.y);
    }

    if (NoiseType != NOISE_FRACTAL) {
      color += abs(noiseResult) * ratio;
    } else {
      color += noiseResult * ratio;
    }

    noiseVec *= 2.0;
    ratio *= 0.5;
    stitch *= 2.0;
  }

  if (NoiseType == NOISE_FRACTAL) {
    color = color * 0.5 + 0.5;
  }
  color = clamp(color, 0.0, 1.0);
  // Premultiply the raw noise, then apply both pointwise slots in order: the base OpType record
  // (shared with an operator FP composed on top of this shader, OP_NONE when absent) followed by
  // the processor-owned Slot1 record (OP_NONE when the processor carries fewer than two operators).
  vec4 result =
      applyPointwiseSlot2(applyPointwiseSlot1(applyPointwiseOp(vec4(color.rgb * color.aaa,
                                                                     color.a))));

#if HAS_COVERAGE
#define TGFX_XP_SRC_COLOR (result * vCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(vCoverage)
#else
#define TGFX_XP_SRC_COLOR result
#endif
#include "xp_output.inc"
}
