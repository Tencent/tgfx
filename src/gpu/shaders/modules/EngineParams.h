// The engine-level uniform fields: written by ProgramInfo's uniform walk itself rather than any
// one processor's module (the render-target adjust and the output swizzle), declared once for
// the build-time reconciliation and referenced by the runtime writer.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct EngineParams {
  static constexpr Param<ParamType::Float4> RTAdjust{"tgfx_RTAdjust"};
  static constexpr Param<ParamType::Int> OutputAlphaSwizzle{"OutputAlphaSwizzle"};

  static constexpr ParamInfo Fields[] = {RTAdjust, OutputAlphaSwizzle};
};

}  // namespace tgfx
