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
  static constexpr Param<ParamType::Int> ColorizerKind{"ColorizerKind"};

  /// The clamped gradient's own record (border colors, mask default, layout branch).
  static constexpr ParamInfo Fields[] = {
      LeftBorderColor, RightBorderColor, LayoutType, Bias, Scale, Start, End};

  static void BindEffect(const FragmentProcessor* processor, ParamWriter* writer);

  /// The conic layout's bias/scale pair; other layouts write nothing.
  static void BindConicLayout(const FragmentProcessor* processor, ParamWriter* writer);

  /// The single-interval colorizer's endpoints and kind selector.
  static void BindColorizer(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
