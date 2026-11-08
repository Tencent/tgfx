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

// The pointwise interpreter's fragment interface, in the declaration language. This is not yet
// wired into the emitter: the chain's entry also carries the config defines, the resource-slot
// glue and the chain main template, which the dedicated-pipeline emitter does not model. The
// declaration exists to prove the language can express the interface and to pin the gaps that the
// migration must close first:
//
//   1. The locations are expressions over the layout and leaf-count dimensions (CHAIN_TEX_LOC_BASE,
//      NTEX), not numbers — VaryingDecl::locationExpr and SamplerDecl::bindingExpr carry them.
//   2. The four leaf samplers are one declared instance group (SamplerDecl::count).
//   3. The XP dst binding is a conditional expression over NTEX.
//   4. The uniform block stays a module placeholder for now: its per-slot arrays
//      (SlotPacked[CHAIN_SLOT_CAPACITY], ChainData) need ArrayField once the block is declared
//      field by field.
//
// The layout macros (GP_ELLIPSE_V_OFFSETS and friends) are printed as their numeric values here;
// the migration must move them into one shared geometry-layout table so the dedicated families and
// the chain allocate the same varying space from one source.

namespace tgfx {

struct ChainInterfaceDecl {
  static constexpr const char* Name = "PointwiseChainShader";
  static constexpr const char* File = "kernels/pointwise_chain.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Module, "pipeline/chain/pointwise_fragment_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
      // GP_LAYOUT 0 (rect layouts): coverage and color follow the leaf coordinates.
      {"float", "vCoverage", 0, "NTEX", "GP_LAYOUT == 0 && HAS_COVERAGE"},
      {"vec4", "vColor", 0, "NTEX + 1", "GP_LAYOUT == 0"},
      // GP_LAYOUT 1: ellipse, sharing gp_ellipse_layout.inc with the dedicated kernel.
      {"vec2", "vEllipseOffsets", 0, nullptr, "GP_LAYOUT == 1"},
      {"vec4", "vEllipseRadii", 1, nullptr, "GP_LAYOUT == 1"},
      {"vec4", "vColor", 2, nullptr, "GP_LAYOUT == 1"},
      // GP_LAYOUT 2: hairline, one edge distance plus the color.
      {"float", "vEdgeDistance", 0, nullptr, "GP_LAYOUT == 2"},
      {"vec4", "vColor", 0, "NTEX + 1", "GP_LAYOUT == 2"},
      // GP_LAYOUT 3/4: complex ellipse fill/stroke.
      {"vec2", "vEllipseOffsets", 0, nullptr, "GP_LAYOUT >= 3"},
      {"vec4", "vEllipseRadii", 1, nullptr, "GP_LAYOUT >= 3"},
      {"vec2", "vEdgeDist", 2, nullptr, "GP_LAYOUT >= 3"},
      {"vec2", "vStrokeWidth", 3, nullptr, "GP_LAYOUT == 4"},
      {"vec4", "vColor", 4, nullptr, "GP_LAYOUT >= 3"},
      // The per-quad subset of a strict draw, in leaf 0's texture space.
      {"vec4", "vTexSubset", 6, nullptr, "HAS_SUBSET"},
      // The four leaf coordinates, placed after the layout's own varyings.
      {"vec3", "TransformedCoords_0", 0, "CHAIN_TEX_LOC_BASE + 0", "NTEX > 0"},
      {"vec3", "TransformedCoords_1", 0, "CHAIN_TEX_LOC_BASE + 1", "NTEX > 0"},
      {"vec3", "TransformedCoords_2", 0, "CHAIN_TEX_LOC_BASE + 2", "NTEX > 0"},
      {"vec3", "TransformedCoords_3", 0, "CHAIN_TEX_LOC_BASE + 3", "NTEX > 0"},
  };

  static constexpr SamplerDecl Samplers[] = {
      {"TextureSampler_", 0, 4, nullptr, "NTEX > 0"},
      {"MaskTextureSampler", 0, 1, "NTEX", "NTEX > 0 || HAS_MASK_TEXTURE"},
  };

  static constexpr FragmentFamilyDecl Decl = {Name,        File,
                                              Block,       std::size(Block),
                                              Varyings,    std::size(Varyings),
                                              Samplers,    std::size(Samplers),
                                              nullptr,     0,
                                              nullptr,     0,
                                              0,           -1,
                                              "NTEX > 0",  "NTEX",
                                              "(NTEX + 1)"};
};

}  // namespace tgfx
