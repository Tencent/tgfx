// The analytic-clip feature module: the runtime clip contract fields (ClipContract on the GLSL
// side) a precompiled kernel reads from a rect effect, declared once and bound from the FP.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct AnalyticClipModule {
  static constexpr Param<ParamType::Float4> LocalRect{"LocalRect"};
  static constexpr Param<ParamType::Float> AntiAlias{"AntiAlias"};
  static constexpr Param<ParamType::Float3x3> DeviceToLocal{"DeviceToLocal"};
  static constexpr Param<ParamType::Float4> Rect{"Rect"};
  static constexpr Param<ParamType::Int> HasClip{"HasClip"};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
