// The analytic-clip feature module: the runtime clip contract fields (ClipContract on the GLSL
// side) a precompiled kernel reads from a rect or rounded-rect effect, declared once and bound
// from the FP. The rect form selects the branch through HasClip 1/3, the rrect form through 2.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct AnalyticClipModule {
  static constexpr Param<ParamType::Float4> LocalRect{"LocalRect"};
  // Per-corner radii in [TL, TR, BR, BL] order, consumed only by the HasClip == 2 branch.
  static constexpr Param<ParamType::Float4> RadiiX{"RadiiX"};
  static constexpr Param<ParamType::Float4> RadiiY{"RadiiY"};
  static constexpr Param<ParamType::Float> AntiAlias{"AntiAlias"};
  static constexpr Param<ParamType::Float3x3> DeviceToLocal{"DeviceToLocal"};
  static constexpr Param<ParamType::Float4> Rect{"Rect"};
  static constexpr Param<ParamType::Int> HasClip{"HasClip"};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
