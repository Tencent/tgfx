// The PorterDuff XferProcessor module: the blend mode and the destination-texture fetch state
// a precompiled kernel's XP reads, declared once and bound from the XP.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class XferProcessor;

struct PorterDuffXPModule {
  static constexpr Param<ParamType::Int> XPBlendMode{"XPBlendMode"};
  static constexpr Param<ParamType::Float2> DstTextureUpperLeft{"DstTextureUpperLeft"};
  static constexpr Param<ParamType::Float2> DstTextureCoordScale{"DstTextureCoordScale"};

  static void Bind(const XferProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
