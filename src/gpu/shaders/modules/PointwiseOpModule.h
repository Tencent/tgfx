// The pointwise-operator feature module: the slot-0 uniform record of the operator a kernel
// applies after sampling (color matrix, luma, alpha threshold). One module serves every operator
// because they all write the same per-slot arrays and differ only in which fields they fill.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

// The pre-slot-array operator fields. They are no longer declared by any kernel (the operator
// parameters ride the chain slot registers, see pointwise_chain_uniforms.inc), so this module is
// not part of the parameter registry; the remaining writers (the fragment processors' legacy
// setData fallback) resolve to nothing. Kept until that fallback retires.
struct PointwiseOpModule {
  static constexpr Param<ParamType::Int> OpType{"OpType"};
  static constexpr Param<ParamType::Float4x4> ColorMatrix{"ColorMatrix"};
  static constexpr Param<ParamType::Float4> ColorVector{"ColorVector"};
  static constexpr Param<ParamType::Float> Kr{"Kr"};
  static constexpr Param<ParamType::Float> Kg{"Kg"};
  static constexpr Param<ParamType::Float> Kb{"Kb"};
  static constexpr Param<ParamType::Float> Threshold{"Threshold"};

  static constexpr ParamInfo Fields[] = {OpType, ColorMatrix, ColorVector, Kr, Kg, Kb, Threshold};
};

}  // namespace tgfx
