// The fused blend path's uniform fields: the blend kernel's mode and operand selectors. The
// child-role and per-child subset fields below serve the runtime-generated programs only (no
// precompiled variant declares them), so they stay out of the reconciled Fields list.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct BlendParams {
  static constexpr Param<ParamType::Int> BlendModeValue{"BlendModeValue"};
  static constexpr Param<ParamType::Int> ChildType{"ChildType"};
  static constexpr Param<ParamType::Int> HasLocalMask{"HasLocalMask"};
  static constexpr Param<ParamType::Float4> Child0Subset{"Child0Subset"};
  static constexpr Param<ParamType::Float4> Child1Subset{"Child1Subset"};
  static constexpr Param<ParamType::Int> Child0AlphaOnly{"Child0AlphaOnly"};
  static constexpr Param<ParamType::Int> Child1AlphaOnly{"Child1AlphaOnly"};

  static constexpr ParamInfo Fields[] = {BlendModeValue, HasLocalMask};
};

}  // namespace tgfx
