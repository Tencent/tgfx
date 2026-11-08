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

/// MeshFillShader: MeshGeometryProcessor + EmptyXferProcessor/PorterDuffXP. A mesh carries
/// optional per-vertex colors (vColor) and a constant-1.0 coverage varying; colorless draws pick
/// the exact Color uniform through HasCommonColor.
struct MeshFillDecl {
  static constexpr const char* Name = "MeshFillShader";
  static constexpr const char* File = "kernels/mesh_fill.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Int, HasCommonColor),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"vec2", "TransformedCoords_0", 0, nullptr, nullptr},
      {"vec4", "vColor", 1, nullptr, nullptr},
      {"float", "vCoverage", 2, nullptr, nullptr},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COLOR", "FILL_COLOR_VARYING_OR_COMMON", nullptr},
      {"FILL_COVERAGE", "FILL_COVERAGE_VERTEX", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {
      Name,    File, Block,  std::size(Block), Varyings, std::size(Varyings),
      nullptr, 0,    Steps,  std::size(Steps), nullptr,  0,
      0,       -1,   nullptr};
};

}  // namespace tgfx
