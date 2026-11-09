// The tiled-source feature module: the sampling state a precompiled tiled kernel reads (tile
// modes per axis, subset and clamp rects, shader-space dimensions, the strict switch and the
// read swizzle), declared once and bound from a TiledTextureEffect.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct TiledSourceModule {
  static constexpr Param<ParamType::Float2> Dimension{"Dimension"};
  static constexpr Param<ParamType::Float4> Subset{"Subset"};
  static constexpr Param<ParamType::Float4> Clamp{"Clamp"};
  static constexpr Param<ParamType::Int> ShaderModeX{"ShaderModeX"};
  static constexpr Param<ParamType::Int> ShaderModeY{"ShaderModeY"};

  static constexpr ParamInfo Fields[] = {Dimension, Subset, Clamp, ShaderModeX, ShaderModeY};
};

}  // namespace tgfx
