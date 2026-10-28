// The texture-source feature module: the per-texture parameters a precompiled kernel reads
// (the subset clamp, the RGBAAA alpha-plane offset, the alpha-only / dual-plane switches),
// declared once and bound from a TextureEffect. Part of the modules/ layer; see
// params/ParamHandle.h for the field contract.
#pragma once
#include <array>
#include "gpu/shaders/params/ParamHandle.h"
#include "gpu/shaders/params/ParamWriter.h"

namespace tgfx {

class FragmentProcessor;

struct TextureSourceModule {
  // Always written by a fill instance; a variant that does not declare a field skips it.
  static constexpr Param<ParamType::Float4> Subset{"Subset", 0};
  static constexpr Param<ParamType::Int> SubsetClampOn{"SubsetClampOn", 1};
  static constexpr Param<ParamType::Float2> AlphaStart{"AlphaStart", 2};
  static constexpr Param<ParamType::Int> AlphaOnly{"AlphaOnly", 3};
  static constexpr Param<ParamType::Int> HasRgbaaa{"HasRgbaaa", 4};

  static constexpr std::array<ParamDesc, 5> FieldDecls = {
      {ParamDescOf(Subset), ParamDescOf(SubsetClampOn), ParamDescOf(AlphaStart),
       ParamDescOf(AlphaOnly), ParamDescOf(HasRgbaaa)}};

  // A dedicated fill kernel's color instance writes every field.
  static constexpr FieldMask FillFields = FieldMask::All(5);

  static void Bind(const FragmentProcessor* processor, ParamWriter* writer);
};

}  // namespace tgfx
