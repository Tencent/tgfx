// PointwiseChainShader fragment shader.
// Evaluates an arbitrary pointwise DAG in one pass. The DAG shape is runtime uniform data: 16
// statically expanded slots each carry a Packed ivec4 (op, two input-slot indices, blend/const
// selector), so any topology with the same texture-leaf count shares one program variant. Texture
// leaves occupy slots 0..NTEX-1 and each samples its own sampler, keeping sampler indexing static.
// TEXTURE_COUNT encodes 0/1/2/4 leaves; a zero-leaf chain evaluates const-color and blend ops
// against the geometry color alone. With HAS_MASK_TEXTURE a device-space alpha mask child is
// sampled after the DAG and before the XP stage.
// Leaf subset rects are runtime uniforms (Subset / Subset_1 / ... following the structural ordinal
// convention); a leaf without a real subset uploads the full texture bounds, so the clamp is a
// no-op. ColorSpaceXform parameters are one shared chain-wide block, so a chain may contain at
// most one color-space op (enforced by the matcher). There is no opcode array, loop, or VM
// dispatch.
#version 450

#ifndef HAS_XP
#define HAS_XP 0
#endif
#ifndef HAS_COVERAGE
#define HAS_COVERAGE 0
#endif
#ifndef HAS_COLOR
#define HAS_COLOR 0
#endif
#ifndef GP_LAYOUT
#define GP_LAYOUT 0
#endif
#ifndef HAS_MASK_TEXTURE
#define HAS_MASK_TEXTURE 0
#endif
#ifndef TEXTURE_COUNT
#define TEXTURE_COUNT 0
#endif

#if TEXTURE_COUNT == 0
#define NTEX 0
#elif TEXTURE_COUNT == 1
#define NTEX 1
#elif TEXTURE_COUNT == 2
#define NTEX 2
#else
#define NTEX 4
#endif

layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  int RootIndex;
  // Slot index of the coverage subtree's root, -1 when the chain has none. When present, the
  // subtree's value replaces the plain coverage modulation (it already carries the GP coverage
  // through the -3 unit input).
  int CoverageRootIndex;
  int SlotCount;
  // Per-leaf subset rects, named for the structural ordinals the TextureEffect writers use.
  vec4 Subset;
#if NTEX >= 2
  vec4 Subset_1;
#endif
#if NTEX >= 4
  vec4 Subset_2;
  vec4 Subset_3;
#endif
  // Shared ColorSpaceXform parameters; at most one slot per chain may be OP_COLOR_SPACE_XFORM.
  int ChainCSFlags;
  vec4 ChainSrcTF0;
  vec4 ChainSrcTF1;
  int ChainSrcTFType;
  vec4 ChainSrcOOTF;
  mat3 ChainColorXform;
  vec4 ChainDstOOTF;
  vec4 ChainDstTF0;
  vec4 ChainDstTF1;
  int ChainDstTFType;
  // Chain-wide AA-rect clip for OP_AARECT_COVERAGE (at most one such slot per chain). The rect is
  // in destination device coordinates and already carries the 0.5 outset for the AA falloff.
  vec4 CoverageRect;
#if HAS_MASK_TEXTURE
  mat3 DeviceCoordMatrix;
#endif
#if GP_LAYOUT == 1
  // EllipseGeometryProcessor writes this for every ellipse draw (stroke vs fill coverage).
  int StrokeEnabled;
#endif

#include "pointwise_chain_uniforms.inc"

// Gradient source block (OP_GRADIENT): at most one gradient slot per chain. All fields are
// runtime uniforms written only when a gradient slot is present; names mirror the dedicated
// gradient kernels with a "Gradient" prefix.
int GradientLayoutType;
float GradientBias;
float GradientScale;
int GradientColorizerKind;
vec4 GradientLeftBorder;
vec4 GradientRightBorder;
vec4 GradientStart;
vec4 GradientEnd;
vec4 GradientScale01;
vec4 GradientBias01;
vec4 GradientScale23;
vec4 GradientBias23;
float GradientThreshold;
int GradientIntervalCount;
vec4 GradientThresholds1_7;
vec4 GradientThresholds9_13;
vec4 GradientScale0_1;
vec4 GradientScale2_3;
vec4 GradientScale4_5;
vec4 GradientScale6_7;
vec4 GradientScale8_9;
vec4 GradientScale10_11;
vec4 GradientScale12_13;
vec4 GradientScale14_15;
vec4 GradientBias0_1;
vec4 GradientBias2_3;
vec4 GradientBias4_5;
vec4 GradientBias6_7;
vec4 GradientBias8_9;
vec4 GradientBias10_11;
vec4 GradientBias12_13;
vec4 GradientBias14_15;
// Leaf index of the LUT gradient texture (a sampler-only child with no DAG slot), read by the
// OP_GRADIENT LUT colorizer branch. -1 when no LUT gradient is present.
int GradientLUTLeaf;

// Tiled leaf support: at most one leaf per chain may need shader-side tiling (wrap or border
// emulation). TiledLeafIndex selects it (-1 = none); the recipe fields are uploaded by the chain
// processor from the resolved sampling, leaving plain leaves on the Subset-clamp path.
int TiledLeafIndex;
int TiledModeX;
int TiledModeY;
vec4 TiledSubset;
vec4 TiledClamp;
vec2 TiledDimension;
int TiledStrict;

#include "xp_uniforms.inc"
};

#if GP_LAYOUT == 1
layout(location = 0) in vec2 vEllipseOffsets;
layout(location = 1) in vec4 vEllipseRadii;
#if HAS_COLOR
layout(location = 2) in vec4 vColor;
#define CHAIN_TEX_LOC_BASE 3
#else
#define CHAIN_TEX_LOC_BASE 2
#endif
#else
#define CHAIN_TEX_LOC_BASE 0
#endif

#if GP_LAYOUT == 0 && HAS_COVERAGE
layout(location = NTEX) in float vCoverage;
#endif
#if GP_LAYOUT == 0 && HAS_COLOR
// QuadGP only: per-vertex color (broadcast for common-color draws) is the geometry color source.
layout(location = NTEX + 1) in vec4 vColor;
#endif

#if NTEX >= 1
layout(location = CHAIN_TEX_LOC_BASE + 0) in vec2 TransformedCoords_0;
#endif
// Gradient coordinate varying at a fixed location beyond every layout's varyings (ellipse with
// color and 4 texture coords tops out at location 6). Always written by the vertex stage; only
// read when a chain slot is OP_GRADIENT.
layout(location = 7) in vec2 GradientCoords;
#if NTEX >= 2
layout(location = CHAIN_TEX_LOC_BASE + 1) in vec2 TransformedCoords_1;
#endif
#if NTEX >= 4
layout(location = CHAIN_TEX_LOC_BASE + 2) in vec2 TransformedCoords_2;
layout(location = CHAIN_TEX_LOC_BASE + 3) in vec2 TransformedCoords_3;
#endif

#if NTEX >= 1
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
#endif
#if NTEX >= 2
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
#endif
#if NTEX >= 4
layout(set = 1, binding = 2) uniform sampler2D TextureSampler_2;
layout(set = 1, binding = 3) uniform sampler2D TextureSampler_3;
#endif
#if HAS_MASK_TEXTURE
// Device-space alpha mask applied after the DAG and before the XP stage (same application point
// as the legacy blend kernel).
layout(set = 1, binding = NTEX) uniform sampler2D MaskTextureSampler;
#define XP_DST_TEX_BINDING (NTEX + 1)
#else
#define XP_DST_TEX_BINDING NTEX
#endif

// The chain kernel blends regardless of XP state, so include the shared blend math directly;
// xp_porter_duff.inc re-includes it under HAS_XP >= 1, guarded against double definition.
#include "ellipse_coverage.inc"
#include "xp_blend_colors.inc"
#include "xp_porter_duff.inc"
#include "xp_porter_duff_fbf.inc"

// Rebind the shared tiled-sampling helpers to the chain's Tiled* uniforms; the include expects
// the standalone names used by the single-texture kernels.
#define ShaderModeX TiledModeX
#define ShaderModeY TiledModeY
#define Subset TiledSubset
#define Clamp TiledClamp
#define Dimension TiledDimension
#include "tiled_sample.inc"
#undef ShaderModeX
#undef ShaderModeY
#undef Subset
#undef Clamp
#undef Dimension

// Leaf fetch: plain leaves clamp to their Subset rect (full bounds = no-op). The tiled leaf, if
// any, goes through the shared tiling math so wrap and clamp-to-border modes match the runtime.
vec4 chainLeafFetch(sampler2D texSampler, vec2 coord, vec4 leafSubset, int leafIndex) {
  if (leafIndex == TiledLeafIndex) {
    vec2 inCoord = vec2(0.0);
    vec2 subsetCoord = vec2(0.0);
    vec2 clampedCoord = vec2(0.0);
    vec2 sampleCoord = tiledMapCoord(coord, TiledStrict != 0, inCoord, subsetCoord, clampedCoord);
    return tiledApplyBorder(texture(texSampler, sampleCoord), inCoord, subsetCoord, clampedCoord);
  }
  return texture(texSampler, clamp(coord, leafSubset.xy, leafSubset.zw));
}

layout(location = 0) out vec4 fragColor;

vec4 chainResults[16];

// Geometry color source: the unconditional inColor attribute on QuadGP (broadcast for
// common-color draws), the Color uniform otherwise.
#if HAS_COLOR
#define TGFX_CHAIN_GEOM_COLOR vColor
#else
#define TGFX_CHAIN_GEOM_COLOR Color
#endif

// The coverage subtree's unit input (slot designator -3): the GP's output coverage, which is
// what the runtime coverage chain starts from. Only GP_LAYOUT=0 chains carry coverage subtrees
// (matcher-enforced); the vec4(1.0) form keeps the define valid for every other variant.
#if GP_LAYOUT == 0 && HAS_COVERAGE
#define TGFX_CHAIN_UNIT_COVERAGE vec4(vCoverage)
#else
#define TGFX_CHAIN_UNIT_COVERAGE vec4(1.0)
#endif

#include "pointwise_chain_eval.inc"

#if NTEX > 0
#define CHAIN_EVAL_LEAF0 evalChainSlotWithTex(0, TextureSampler_0, TransformedCoords_0, Subset, 0)
#endif
#if NTEX > 1
#define CHAIN_EVAL_LEAF1 evalChainSlotWithTex(1, TextureSampler_1, TransformedCoords_1, Subset_1, 1)
#endif
#if NTEX > 2
#define CHAIN_EVAL_LEAF2 evalChainSlotWithTex(2, TextureSampler_2, TransformedCoords_2, Subset_2, 2)
#endif
#if NTEX > 3
#define CHAIN_EVAL_LEAF3 evalChainSlotWithTex(3, TextureSampler_3, TransformedCoords_3, Subset_3, 3)
#endif

void main() {
  // Active slots are contiguous from 0 by construction (texture leaves first, then the rest in
  // topological order, root last), so a uniform guard skips the whole evaluation of unused slots —
  // including the Packed read — instead of relying on the OP_NONE early-out alone.
  // Texture leaves occupy slots 0..NTEX-1 with statically bound samplers; the remaining op slots
  // share one implementation, and the SlotCount-bound loop keeps that implementation single in the
  // binary.
#if NTEX > 0
  chainResults[0] = CHAIN_EVAL_LEAF0;
#endif
#if NTEX > 1
  chainResults[1] = CHAIN_EVAL_LEAF1;
#endif
#if NTEX > 2
  chainResults[2] = CHAIN_EVAL_LEAF2;
#endif
#if NTEX > 3
  chainResults[3] = CHAIN_EVAL_LEAF3;
#endif
  for (int i = NTEX; i < SlotCount; ++i) {
    chainResults[i] = evalChainSlotNoTex(i);
  }
  vec4 result = chainResults[RootIndex];

#if HAS_MASK_TEXTURE
  // Device-space mask multiply, applied after the DAG and before coverage/XP (same application
  // point as the legacy blend kernel).
  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);
  result *= texture(MaskTextureSampler, maskCoord.xy).r;
#endif

#if GP_LAYOUT == 1
  // Ellipse edge AA, computed per pixel from the ellipse varyings (verbatim shared math).
  highp float gpCoverage = ellipseEdgeCoverage(vEllipseOffsets, vEllipseRadii, StrokeEnabled);
#define TGFX_XP_SRC_COLOR (result * gpCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE vec4(gpCoverage)
#elif HAS_COVERAGE
  // A coverage subtree's root already carries the GP coverage (fed in through the -3 unit
  // input), so it replaces the plain vCoverage modulation instead of doubling it.
  vec4 finalCoverage = CoverageRootIndex >= 0 ? chainResults[CoverageRootIndex] : vec4(vCoverage);
#define TGFX_XP_SRC_COLOR (result * finalCoverage)
#define TGFX_XP_SRC_UNPREMUL result
#define TGFX_XP_COVERAGE finalCoverage
#else
#define TGFX_XP_SRC_COLOR (CoverageRootIndex >= 0 ? result * chainResults[CoverageRootIndex] : result)
#endif
#include "xp_output.inc"
}
