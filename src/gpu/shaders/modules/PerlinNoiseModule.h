// The Perlin-noise feature module: the noise parameters and the pointwise-slot records the
// precompiled noise kernel reads, declared once and bound from the noise processor.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct PerlinNoiseModule {
  static constexpr Param<ParamType::Float2> BaseFrequency{"baseFrequency"};
  static constexpr Param<ParamType::Float2> StitchData{"stitchData"};
  static constexpr Param<ParamType::Int> NoiseType{"NoiseType"};
  static constexpr Param<ParamType::Int> NumOctaves{"NumOctaves"};
  static constexpr Param<ParamType::Int> StitchTiles{"StitchTiles"};
  static constexpr Param<ParamType::Int> OpType{"OpType"};
  static constexpr Param<ParamType::Int> PointwiseSlotCount{"PointwiseSlotCount"};

  static constexpr ParamInfo Fields[] = {BaseFrequency, StitchData, NoiseType,         NumOctaves,
                                         StitchTiles,   OpType,     PointwiseSlotCount};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
