// The device-mask feature module: the device-space mask coordinate matrix, the runtime apply
// switch, and the never-read subset decoy a shared kernel declares so this effect's own
// Subset/AlphaOnly writes do not clobber the leaf processor's same-named fields.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct DeviceMaskModule {
  static constexpr Param<ParamType::Float3x3> DeviceCoordMatrix{"DeviceCoordMatrix"};
  static constexpr Param<ParamType::Int> HasDeviceMask{"HasDeviceMask"};
  static constexpr Param<ParamType::Float4> DeviceMaskSubset{"DeviceMaskSubset"};
  // The non-decoy fallbacks: no-op bounds so a bare mask neither clamps nor clips the sample.
  static constexpr Param<ParamType::Float4> Subset{"Subset"};
  static constexpr Param<ParamType::Int> AlphaOnly{"AlphaOnly"};
  static constexpr Param<ParamType::Float4> Rect{"Rect"};

  static constexpr ParamInfo Fields[] = {DeviceCoordMatrix, DeviceMaskSubset, Subset, AlphaOnly,
                                         Rect};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
