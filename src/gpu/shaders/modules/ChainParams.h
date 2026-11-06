// The pointwise chain kernel's own uniform fields: the fused interpreter's per-slot records, the
// runtime clip channels, and the extended resource blocks (gradient parameters, tiled recipes,
// noise selectors). Declared once so the chain writers and the build-time reconciliation share
// one name source; the field types mirror the GLSL uniform-block declarations.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct ChainParams {
  // The per-slot records of the fused interpreter (SlotPacked is an ivec4 table). The parameter
  // registers are named by function, not by operator: a slot holds exactly one operator, so its
  // whole parameter area belongs to it. The allocation table (which operator reads which register)
  // lives in pointwise_chain_uniforms.inc; a new operator picks a free register there.
  static constexpr Param<ParamType::Int4> SlotPacked{"SlotPacked"};

  // The per-slot data pool: one block of words per slot, holding whichever operator's parameters
  // that slot carries. The block layout lives only in contract/ChainOps.h.
  static constexpr Param<ParamType::Float4> ChainData{"ChainData"};

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
  static constexpr Param<ParamType::Int4> NoiseModeOctaves{"NoiseModeOctaves"};

  // The gradient resource kind's parameter block.
  static constexpr Param<ParamType::Int> GradientLayoutType{"GradientLayoutType"};
  static constexpr Param<ParamType::Int> GradientColorizerKind{"GradientColorizerKind"};
  static constexpr Param<ParamType::Int> GradientIntervalCount{"GradientIntervalCount"};

  // The unrolled colorizer's per-interval parameter pairs (vec4 scale and bias each).

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
                                         ChainData,
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
                                          NoiseModeOctaves,
                                         GradientLayoutType,
                                         GradientColorizerKind,
                                         GradientIntervalCount};
};

}  // namespace tgfx
