// The tiled-source feature module: the sampling state a precompiled tiled kernel reads (tile
// modes per axis, subset and clamp rects, shader-space dimensions, alpha-only / strict
// switches, and the read swizzle), declared once and bound from a TiledTextureEffect.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct TiledSourceModule {
  static constexpr Param<ParamType::Int> ReadSwizzle{"ReadSwizzle"};
  static constexpr Param<ParamType::Float2> Dimension{"Dimension"};
  static constexpr Param<ParamType::Float4> Subset{"Subset"};
  static constexpr Param<ParamType::Float4> Clamp{"Clamp"};
  static constexpr Param<ParamType::Int> ShaderModeX{"ShaderModeX"};
  static constexpr Param<ParamType::Int> ShaderModeY{"ShaderModeY"};
  static constexpr Param<ParamType::Int> AlphaOnly{"AlphaOnly"};
  static constexpr Param<ParamType::Int> Strict{"Strict"};

  static constexpr ParamInfo Fields[] = {ReadSwizzle, Dimension,   Subset,    Clamp,
                                         ShaderModeX, ShaderModeY, AlphaOnly, Strict};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
