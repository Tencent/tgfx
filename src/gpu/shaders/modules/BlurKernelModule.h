// The blur-kernel feature module: the kernel table, the sample step and the child-shape flags a
// precompiled blur kernel reads, declared once and bound from the blur processor.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct BlurKernelModule {
  static constexpr Param<ParamType::Float4Array> Kernel{"Kernel"};
  static constexpr Param<ParamType::Int> Radius{"Radius"};
  static constexpr Param<ParamType::Float2> Step{"Step"};
  static constexpr Param<ParamType::Int> TiledChild{"TiledChild"};
  static constexpr Param<ParamType::Int> AlphaChild{"AlphaChild"};

  static constexpr ParamInfo Fields[] = {Kernel, Radius, Step, TiledChild, AlphaChild};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
