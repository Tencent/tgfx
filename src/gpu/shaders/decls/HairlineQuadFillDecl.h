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

/// HairlineQuadShader: HairlineQuadGeometryProcessor + EmptyXferProcessor/PorterDuffXP. The
/// Loop-Blinn edge (vHairQuadEdge) is the coverage; the Coverage/AAEnabled uniforms scale it.
struct HairlineQuadFillDecl {
  static constexpr const char* Name = "HairlineQuadShader";
  static constexpr const char* File = "kernels/hairline_quad.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Float, Coverage),
      TGFX_DECL_ELEMENT(Field, Int, AAEnabled),
      TGFX_DECL_ELEMENT(Field, Float4, Rect),
      TGFX_DECL_ELEMENT(Field, Int, HasClip),
      TGFX_DECL_ELEMENT(Module, "coverage/rrect_clip_uniforms.inc"),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      {"vec4", "vHairQuadEdge", 0, nullptr, nullptr},
  };

  static constexpr StepDecl Steps[] = {
      {"FILL_COVERAGE", "FILL_COVERAGE_HAIRLINE_QUAD", nullptr},
      {"FILL_CLIP", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {
      Name,    File, Block,  std::size(Block), Varyings, std::size(Varyings),
      nullptr, 0,    Steps,  std::size(Steps), nullptr,  0,
      0,       -1,   nullptr};
};

}  // namespace tgfx
