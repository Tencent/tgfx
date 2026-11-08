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

/// SolidColorFillShader: DefaultGeometryProcessor + EmptyXferProcessor/PorterDuffXP, no fragment
/// processor. The color is the GP's Color uniform; the coverage is the vCoverage varying (a
/// constant 1.0 for non-AA path triangles). Field order matches the handwritten block, which is
/// the std140 order the bundles were built with.
struct SolidColorFillDecl {
  static constexpr const char* Name = "SolidColorFillShader";
  static constexpr const char* File = "kernels/solid_color_fill.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Field, Float4, Color),
      TGFX_DECL_ELEMENT(Field, Float4, Rect),
      TGFX_DECL_ELEMENT(Field, Int, HasClip),
      TGFX_DECL_ELEMENT(Module, "coverage/rrect_clip_uniforms.inc"),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
      TGFX_DECL_ELEMENT(Field, Int, OutputAlphaSwizzle),
  };

  static constexpr VaryingDecl Varyings[] = {{"float", "vCoverage", 0, nullptr, nullptr}};

  // Only the steps that differ from fill_fragment_main.inc's defaults are listed: the color source
  // stays the template's FILL_COLOR_UNIFORM (the DefaultGP provides no vColor varying).
  static constexpr StepDecl Steps[] = {
      {"FILL_COVERAGE", "FILL_COVERAGE_VERTEX", nullptr},
      {"FILL_CLIP", "1", nullptr},
      {"FILL_OUTPUT_SWIZZLE", "1", nullptr},
  };

  static constexpr FragmentFamilyDecl Decl = {
      Name,    File, Block,  std::size(Block), Varyings, std::size(Varyings),
      nullptr, 0,    Steps,  std::size(Steps), nullptr,  0,
      0,       -1,   nullptr};
};

}  // namespace tgfx
