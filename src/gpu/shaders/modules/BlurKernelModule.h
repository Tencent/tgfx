// The blur-kernel feature module: the child-shape flag the remaining precompiled blur kernel
// (the glass UDF tent blur) reads. The Gaussian family's kernel table, radius and step moved to
// its own declared module.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct BlurKernelModule {
  static constexpr Param<ParamType::Int> TiledChild{"TiledChild"};

  static constexpr ParamInfo Fields[] = {TiledChild};
};

}  // namespace tgfx
