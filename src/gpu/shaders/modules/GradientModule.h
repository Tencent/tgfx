// The gradient feature module: the boundary colors and branch selector of a clamped gradient,
// plus the two children of the precompiled gradient branch (conic layout parameters and the
// single-interval colorizer endpoints). One module file because they form one gradient chain.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct GradientModule {
  static constexpr Param<ParamType::Float4> LeftBorderColor{"leftBorderColor"};
  static constexpr Param<ParamType::Float4> RightBorderColor{"rightBorderColor"};
  static constexpr Param<ParamType::Int> LayoutType{"LayoutType"};
  static constexpr Param<ParamType::Float> Bias{"Bias"};
  static constexpr Param<ParamType::Float> Scale{"Scale"};
  static constexpr Param<ParamType::Float4> Start{"start"};
  static constexpr Param<ParamType::Float4> End{"end"};

  /// The clamped gradient's own record: border colors, the layout branch, and the conic layout's
  /// bias/scale pair (other layouts write nothing) plus the single-interval colorizer's endpoints
  /// (other colorizers write nothing).
  static constexpr ParamInfo Fields[] = {
      LeftBorderColor, RightBorderColor, LayoutType, Bias, Scale, Start, End};
};

}  // namespace tgfx
