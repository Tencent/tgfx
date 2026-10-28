// The pointwise chain kernel's own uniform fields: the fused interpreter's per-slot records, the
// runtime clip channels, and the extended resource blocks (gradient parameters, tiled recipes,
// noise selectors). Declared once so the chain writers and the build-time reconciliation share
// one name source; the field types mirror the GLSL uniform-block declarations.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct ChainParams {
  // The per-slot records of the fused interpreter (SlotPacked is an ivec4 table).
  static constexpr Param<ParamType::Int4> SlotPacked{"SlotPacked"};
  static constexpr Param<ParamType::Float4x4> SlotColorMatrix{"SlotColorMatrix"};
  static constexpr Param<ParamType::Float4> SlotColorVector{"SlotColorVector"};
  static constexpr Param<ParamType::Float4> SlotLumaThresh{"SlotLumaThresh"};
  static constexpr Param<ParamType::Float4> SlotConstColor{"SlotConstColor"};

  // The runtime clip channels the chain composites as its coverage input.
  static constexpr Param<ParamType::Float4> CoverageRect{"CoverageRect"};
  static constexpr Param<ParamType::Float4> CoverageLocalRect{"CoverageLocalRect"};
  static constexpr Param<ParamType::Float3x3> CoverageLocalDeviceToLocal{
      "CoverageLocalDeviceToLocal"};
  static constexpr Param<ParamType::Float4> CoverageRRectRect{"CoverageRRectRect"};
  static constexpr Param<ParamType::Float4> CoverageRRectRadiiX{"CoverageRRectRadiiX"};
  static constexpr Param<ParamType::Float4> CoverageRRectRadiiY{"CoverageRRectRadiiY"};
  static constexpr Param<ParamType::Float> CoverageRRectAntiAlias{"CoverageRRectAntiAlias"};
  static constexpr Param<ParamType::Float3x3> CoverageRRectDeviceToLocal{
      "CoverageRRectDeviceToLocal"};

  // The interpreter's shape selectors.
  static constexpr Param<ParamType::Int> CoordSourceMask{"CoordSourceMask"};
  static constexpr Param<ParamType::Int> HasMaskTexture{"HasMaskTexture"};
  static constexpr Param<ParamType::Int> RootIndex{"RootIndex"};
  static constexpr Param<ParamType::Int> CoverageRootIndex{"CoverageRootIndex"};
  static constexpr Param<ParamType::Int> ClipCoverageRegister{"ClipCoverageRegister"};
  static constexpr Param<ParamType::Int> SlotCount{"SlotCount"};
  static constexpr Param<ParamType::Int> DeviceLeafMask{"DeviceLeafMask"};
  static constexpr Param<ParamType::Float3x3> DeviceLeafCoordMatrix{"DeviceLeafCoordMatrix"};
  static constexpr Param<ParamType::Int4> LeafReadSwizzles{"LeafReadSwizzles"};
  static constexpr Param<ParamType::Int> GradientLUTLeaf{"GradientLUTLeaf"};
  static constexpr Param<ParamType::Int> PointwiseSlotCount{"PointwiseSlotCount"};

  // The extended resource kinds' recipe tables.
  static constexpr Param<ParamType::Float4> TiledRecipeSubsets{"TiledRecipeSubsets"};
  static constexpr Param<ParamType::Float4> TiledRecipeClamps{"TiledRecipeClamps"};
  static constexpr Param<ParamType::Float2> TiledRecipeDimensions{"TiledRecipeDimensions"};
  static constexpr Param<ParamType::Int4> TiledRecipeModes{"TiledRecipeModes"};
  static constexpr Param<ParamType::Int> TiledLeafIndex{"TiledLeafIndex"};
  static constexpr Param<ParamType::Int> TiledLeafIndex2{"TiledLeafIndex2"};

  // The plain chain's single tiled recipe, in both suffix forms ("" and "2").
  static constexpr Param<ParamType::Int> TiledModeX{"TiledModeX"};
  static constexpr Param<ParamType::Int> TiledModeY{"TiledModeY"};
  static constexpr Param<ParamType::Float4> TiledSubset{"TiledSubset"};
  static constexpr Param<ParamType::Float4> TiledClamp{"TiledClamp"};
  static constexpr Param<ParamType::Float2> TiledDimension{"TiledDimension"};
  static constexpr Param<ParamType::Int> TiledStrict{"TiledStrict"};
  static constexpr Param<ParamType::Int> TiledModeX2{"TiledModeX2"};
  static constexpr Param<ParamType::Int> TiledModeY2{"TiledModeY2"};
  static constexpr Param<ParamType::Float4> TiledSubset2{"TiledSubset2"};
  static constexpr Param<ParamType::Float4> TiledClamp2{"TiledClamp2"};
  static constexpr Param<ParamType::Float2> TiledDimension2{"TiledDimension2"};
  static constexpr Param<ParamType::Int> TiledStrict2{"TiledStrict2"};

  // The noise resource kind's selectors.
  static constexpr Param<ParamType::Float4> NoiseFrequencyStitch{"NoiseFrequencyStitch"};
  static constexpr Param<ParamType::Int4> NoiseModeOctaves{"NoiseModeOctaves"};

  // The gradient resource kind's parameter block.
  static constexpr Param<ParamType::Int> GradientLayoutType{"GradientLayoutType"};
  static constexpr Param<ParamType::Float> GradientBias{"GradientBias"};
  static constexpr Param<ParamType::Float> GradientScale{"GradientScale"};
  static constexpr Param<ParamType::Int> GradientColorizerKind{"GradientColorizerKind"};
  static constexpr Param<ParamType::Float4> GradientLeftBorder{"GradientLeftBorder"};
  static constexpr Param<ParamType::Float4> GradientRightBorder{"GradientRightBorder"};
  static constexpr Param<ParamType::Float4> GradientStart{"GradientStart"};
  static constexpr Param<ParamType::Float4> GradientEnd{"GradientEnd"};
  static constexpr Param<ParamType::Float4> GradientScale01{"GradientScale01"};
  static constexpr Param<ParamType::Float4> GradientBias01{"GradientBias01"};
  static constexpr Param<ParamType::Float4> GradientScale23{"GradientScale23"};
  static constexpr Param<ParamType::Float4> GradientBias23{"GradientBias23"};
  static constexpr Param<ParamType::Float> GradientThreshold{"GradientThreshold"};
  static constexpr Param<ParamType::Int> GradientIntervalCount{"GradientIntervalCount"};
  static constexpr Param<ParamType::Float4> GradientThresholds1_7{"GradientThresholds1_7"};
  static constexpr Param<ParamType::Float4> GradientThresholds9_13{"GradientThresholds9_13"};

  // The unrolled colorizer's per-interval parameter pairs (vec4 scale and bias each).
  static constexpr Param<ParamType::Float4> GradientScale0_1{"GradientScale0_1"};
  static constexpr Param<ParamType::Float4> GradientBias0_1{"GradientBias0_1"};
  static constexpr Param<ParamType::Float4> GradientScale2_3{"GradientScale2_3"};
  static constexpr Param<ParamType::Float4> GradientBias2_3{"GradientBias2_3"};
  static constexpr Param<ParamType::Float4> GradientScale4_5{"GradientScale4_5"};
  static constexpr Param<ParamType::Float4> GradientBias4_5{"GradientBias4_5"};
  static constexpr Param<ParamType::Float4> GradientScale6_7{"GradientScale6_7"};
  static constexpr Param<ParamType::Float4> GradientBias6_7{"GradientBias6_7"};
  static constexpr Param<ParamType::Float4> GradientScale8_9{"GradientScale8_9"};
  static constexpr Param<ParamType::Float4> GradientBias8_9{"GradientBias8_9"};
  static constexpr Param<ParamType::Float4> GradientScale10_11{"GradientScale10_11"};
  static constexpr Param<ParamType::Float4> GradientBias10_11{"GradientBias10_11"};
  static constexpr Param<ParamType::Float4> GradientScale12_13{"GradientScale12_13"};
  static constexpr Param<ParamType::Float4> GradientBias12_13{"GradientBias12_13"};
  static constexpr Param<ParamType::Float4> GradientScale14_15{"GradientScale14_15"};
  static constexpr Param<ParamType::Float4> GradientBias14_15{"GradientBias14_15"};

  // The name pair table the per-slot gradient upload walks.
  struct GradientPairNames {
    const char* scale;
    const char* bias;
  };
  static constexpr GradientPairNames GradientPairs[8] = {
      {GradientScale0_1.name, GradientBias0_1.name},
      {GradientScale2_3.name, GradientBias2_3.name},
      {GradientScale4_5.name, GradientBias4_5.name},
      {GradientScale6_7.name, GradientBias6_7.name},
      {GradientScale8_9.name, GradientBias8_9.name},
      {GradientScale10_11.name, GradientBias10_11.name},
      {GradientScale12_13.name, GradientBias12_13.name},
      {GradientScale14_15.name, GradientBias14_15.name}};

  // One tiled recipe's field names, for the suffix-form upload walk.
  struct TiledRecipeNames {
    const char* modeX;
    const char* modeY;
    const char* subset;
    const char* clamp;
    const char* dimension;
    const char* strict;
  };
  static constexpr TiledRecipeNames TiledRecipe = {TiledModeX.name,     TiledModeY.name,
                                                   TiledSubset.name,    TiledClamp.name,
                                                   TiledDimension.name, TiledStrict.name};
  static constexpr TiledRecipeNames TiledRecipe2 = {TiledModeX2.name,     TiledModeY2.name,
                                                    TiledSubset2.name,    TiledClamp2.name,
                                                    TiledDimension2.name, TiledStrict2.name};

  static constexpr ParamInfo Fields[] = {SlotPacked,
                                         SlotColorMatrix,
                                         SlotColorVector,
                                         SlotLumaThresh,
                                         SlotConstColor,
                                         CoverageRect,
                                         CoverageLocalRect,
                                         CoverageLocalDeviceToLocal,
                                         CoverageRRectRect,
                                         CoverageRRectRadiiX,
                                         CoverageRRectRadiiY,
                                         CoverageRRectAntiAlias,
                                         CoverageRRectDeviceToLocal,
                                         CoordSourceMask,
                                         HasMaskTexture,
                                         RootIndex,
                                         CoverageRootIndex,
                                         ClipCoverageRegister,
                                         SlotCount,
                                         DeviceLeafMask,
                                         DeviceLeafCoordMatrix,
                                         LeafReadSwizzles,
                                         GradientLUTLeaf,
                                         PointwiseSlotCount,
                                         TiledRecipeSubsets,
                                         TiledRecipeClamps,
                                         TiledRecipeDimensions,
                                         TiledRecipeModes,
                                         TiledLeafIndex,
                                         TiledLeafIndex2,
                                         TiledModeX,
                                         TiledModeY,
                                         TiledSubset,
                                         TiledClamp,
                                         TiledDimension,
                                         TiledStrict,
                                         TiledModeX2,
                                         TiledModeY2,
                                         TiledSubset2,
                                         TiledClamp2,
                                         TiledDimension2,
                                         TiledStrict2,
                                         NoiseFrequencyStitch,
                                         NoiseModeOctaves,
                                         GradientLayoutType,
                                         GradientBias,
                                         GradientScale,
                                         GradientColorizerKind,
                                         GradientLeftBorder,
                                         GradientRightBorder,
                                         GradientStart,
                                         GradientEnd,
                                         GradientScale01,
                                         GradientBias01,
                                         GradientScale23,
                                         GradientBias23,
                                         GradientThreshold,
                                         GradientIntervalCount,
                                         GradientThresholds1_7,
                                         GradientThresholds9_13,
                                         GradientScale0_1,
                                         GradientBias0_1,
                                         GradientScale2_3,
                                         GradientBias2_3,
                                         GradientScale4_5,
                                         GradientBias4_5,
                                         GradientScale6_7,
                                         GradientBias6_7,
                                         GradientScale8_9,
                                         GradientBias8_9,
                                         GradientScale10_11,
                                         GradientBias10_11,
                                         GradientScale12_13,
                                         GradientBias12_13,
                                         GradientScale14_15,
                                         GradientBias14_15};
};

}  // namespace tgfx
