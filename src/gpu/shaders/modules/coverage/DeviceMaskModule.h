// The device-mask coverage module (seed): the runtime switch that selects whether the fill
// kernels apply their always-declared mask sampler. A present mask FP's own writer sets it to
// 1 during traversal; the plan binds the default (0) as an absent instance for programs whose
// structure carries no mask FP. Grows into the full coverage module set in M3.
#pragma once
#include <array>
#include "gpu/shaders/params/ParamHandle.h"
#include "gpu/shaders/params/ParamWriter.h"

namespace tgfx {

class FragmentProcessor;

struct DeviceMaskModule {
  static constexpr Param<ParamType::Int> HasDeviceMask{"HasDeviceMask", 0};

  static constexpr std::array<ParamDesc, 1> FieldDecls = {{ParamDescOf(HasDeviceMask)}};

  static constexpr FieldMask DefaultFields = FieldMask::All(1);

  // Receives a null processor (addAbsent): writes the runtime default.
  static void Bind(const FragmentProcessor*, ParamWriter* writer) {
    writer->set(HasDeviceMask, 0);
  }
};

}  // namespace tgfx
