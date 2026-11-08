/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <iterator>
#include "gpu/shaders/decls/ModuleDecl.h"

namespace tgfx {

/// MaskFillShader: DefaultGeometryProcessor + one alpha-only TextureEffect coverage FP + XP. The
/// coverage sampler must be named TextureSampler_0: the precompiled path binds a fragment
/// processor's texture by its (suffix-stripped) runtime name.
struct MaskFillDecl {
  static constexpr const char* Name = "MaskFillShader";
  static constexpr const char* File = "kernels/mask_fill.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Int, OutputAlphaSwizzle),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"vec2", "TransformedCoords_0", 0, nullptr, nullptr},
      {"float", "vCoverage", 1, nullptr, nullptr},
  };

  static constexpr SamplerDecl Samplers[] = {
      {"TextureSampler_0", 0, 1, nullptr, nullptr},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COVERAGE", "FILL_COVERAGE_VERTEX_MASK", nullptr},
      {"FILL_OUTPUT_SWIZZLE", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {Name,     File,
                                              Block,    std::size(Block),
                                              Varyings, std::size(Varyings),
                                              Samplers, std::size(Samplers),
                                              Steps,    std::size(Steps),
                                              nullptr,  0,
                                              1,        -1,
                                              nullptr};
};

}  // namespace tgfx
