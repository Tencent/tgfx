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

/// ComplexEllipseFillShader: ComplexEllipseGeometryProcessor + EmptyXferProcessor/PorterDuffXP.
/// Offset-based region dispatch (corner/edge/center). The device mask is a runtime uniform with a
/// padded dummy sampler, not a dimension; every extra dimension would double the whole family.
struct ComplexEllipseFillDecl {
  static constexpr const char* Name = "ComplexEllipseFillShader";
  static constexpr const char* File = "kernels/complex_ellipse_fill.frag";

  static constexpr const char* Defines[] = {"HAS_RUNTIME_CLIP=1", "HAS_RUNTIME_DEVICE_MASK=1"};

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Int, HasCommonColor),
      TGFX_DECL_ELEMENT(Module, "coverage/coverage_uniforms.inc"),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"vec2", "vEllipseOffsets", 0, nullptr, nullptr},
      {"vec4", "vEllipseRadii", 1, nullptr, nullptr},
      {"vec2", "vEdgeDist", 2, nullptr, nullptr},
      {"vec4", "vColor", 3, nullptr, nullptr},
      {"vec2", "vStrokeWidth", 4, nullptr, "STROKE"},
  };

  static constexpr SamplerDecl Samplers[] = {
      {"MaskTextureSampler", 0, 1, nullptr, nullptr},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COLOR", "FILL_COLOR_VARYING_OR_COMMON", nullptr},
      {"FILL_COVERAGE", "FILL_COVERAGE_COMPLEX_ELLIPSE", nullptr},
      {"FILL_DEVICE_MASK", "FILL_DEVICE_MASK_RUNTIME", nullptr},
      {"FILL_CLIP", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {Name,     File,
                                              Block,    std::size(Block),
                                              Varyings, std::size(Varyings),
                                              Samplers, std::size(Samplers),
                                              Steps,    std::size(Steps),
                                              Defines,  std::size(Defines),
                                              1,        -1,
                                              nullptr};
};

}  // namespace tgfx
