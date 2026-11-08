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

/// QuadColorFillShader: QuadPerEdgeAAGeometryProcessor + EmptyXferProcessor/PorterDuffXP. The
/// color comes only from the vColor varying and the coverage only from vCoverage; a device mask
/// (HAS_MASK_TEXTURE) adds a mat3 and a sampler.
struct QuadColorFillDecl {
  static constexpr const char* Name = "QuadColorFillShader";
  static constexpr const char* File = "kernels/quad_color_fill.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Rect),
      TGFX_DECL_ELEMENT(Field, Int, HasClip),
      TGFX_DECL_ELEMENT(Module, "coverage/rrect_clip_uniforms.inc"),
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Int, HasCommonColor),
      TGFX_DECL_ELEMENT_ConditionalField(Float3x3, DeviceCoordMatrix, "HAS_MASK_TEXTURE"),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"float", "vCoverage", 0, nullptr, nullptr},
      {"vec4", "vColor", 1, nullptr, nullptr},
  };

  static constexpr SamplerDecl Samplers[] = {
      {"MaskTextureSampler", 0, 1, nullptr, "HAS_MASK_TEXTURE"},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COLOR", "FILL_COLOR_VARYING_OR_COMMON", nullptr},
      {"FILL_COVERAGE", "FILL_COVERAGE_VERTEX", nullptr},
      {"FILL_DEVICE_MASK", "FILL_DEVICE_MASK_ALWAYS", "HAS_MASK_TEXTURE"},
      {"FILL_CLIP", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {Name,
                                              File,
                                              Block,
                                              std::size(Block),
                                              Varyings,
                                              std::size(Varyings),
                                              Samplers,
                                              std::size(Samplers),
                                              Steps,
                                              std::size(Steps),
                                              nullptr,
                                              0,
                                              0,
                                              1,
                                              "HAS_MASK_TEXTURE"};
};

}  // namespace tgfx
