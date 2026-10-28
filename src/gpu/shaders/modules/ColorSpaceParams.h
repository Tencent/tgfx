// The color-space transform steps' uniform fields, shared by the precompiled kernels and the
// runtime-generated code through ColorSpaceXformHelper. The helper's prefix (e.g. "Chain") and
// per-slot array forms address these names with a prefix or an array element index.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

struct ColorSpaceParams {
  static constexpr Param<ParamType::Int> CSFlags{"CSFlags"};
  static constexpr Param<ParamType::Float4> SrcTF0{"SrcTF0"};
  static constexpr Param<ParamType::Float4> SrcTF1{"SrcTF1"};
  static constexpr Param<ParamType::Int> SrcTFType{"SrcTFType"};
  static constexpr Param<ParamType::Float4> SrcOOTF{"SrcOOTF"};
  static constexpr Param<ParamType::Float3x3> ColorXform{"ColorXform"};
  static constexpr Param<ParamType::Float4> DstOOTF{"DstOOTF"};
  static constexpr Param<ParamType::Float4> DstTF0{"DstTF0"};
  static constexpr Param<ParamType::Float4> DstTF1{"DstTF1"};
  static constexpr Param<ParamType::Int> DstTFType{"DstTFType"};

  static constexpr ParamInfo Fields[] = {CSFlags,    SrcTF0,  SrcTF1, SrcTFType, SrcOOTF,
                                         ColorXform, DstOOTF, DstTF0, DstTF1,    DstTFType};
};

}  // namespace tgfx
