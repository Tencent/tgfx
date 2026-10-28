// The texture-source feature module: the per-texture parameters a precompiled kernel reads
// (the subset clamp, the RGBAAA alpha-plane offset, the alpha-only / dual-plane switches),
// declared once and bound from a TextureEffect. See ParamHandle.h for the field contract.
#pragma once
#include "gpu/shaders/modules/ParamHandle.h"

namespace tgfx {

class FragmentProcessor;

struct TextureSourceModule {
  static constexpr Param<ParamType::Float4> Subset{"Subset"};
  static constexpr Param<ParamType::Int> SubsetClampOn{"SubsetClampOn"};
  static constexpr Param<ParamType::Float2> AlphaStart{"AlphaStart"};
  static constexpr Param<ParamType::Int> AlphaOnly{"AlphaOnly"};
  static constexpr Param<ParamType::Int> HasRgbaaa{"HasRgbaaa"};
  // YUV-only: the limited-range flag and the vec4-aligned conversion matrix.
  static constexpr Param<ParamType::Int> YUVLimitedRange{"YUVLimitedRange"};
  static constexpr Param<ParamType::Float3x3> Mat3ColorConversion{"Mat3ColorConversion"};

  static constexpr ParamInfo Fields[] = {Subset,    SubsetClampOn,   AlphaStart,         AlphaOnly,
                                         HasRgbaaa, YUVLimitedRange, Mat3ColorConversion};

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
