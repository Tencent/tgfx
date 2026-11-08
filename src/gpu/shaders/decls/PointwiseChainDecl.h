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

// The pointwise interpreter's fragment entry, declared. Everything the entry used to spell by
// hand is here: the uniform block (core, the resource profile's modules, XP), the interface
// (five geometry layouts, the leaf coordinates, the mask), the generated leaf reader, the two
// tool-generated pragmas, and the pipeline template. The math modules stay as includes; the
// entry itself is generated.
//
// The uniform block still keeps its core fields behind a module include: the per-slot tables are
// arrays whose capacity comes from the injected resource profile, and declaring them field by
// field is the next step. The varying-location tables are the geometry layouts' own, shared with
// the dedicated kernels, so the two sides cannot drift.

namespace tgfx {

struct PointwiseChainDecl {
  static constexpr const char* Name = "PointwiseChainShader";
  static constexpr const char* File = "kernels/pointwise_chain.frag";

  static constexpr FragmentElement Block[] = {
      TGFX_DECL_ELEMENT(Module, "pipeline/chain/pointwise_fragment_uniforms.inc"),
      TGFX_DECL_ELEMENT_ConditionalModule("pipeline/chain/pointwise_gradient_uniforms.inc",
                                          "RESOURCE_KIND == 1"),
      TGFX_DECL_ELEMENT_ConditionalModule("pipeline/chain/pointwise_tiled3_uniforms.inc",
                                          "RESOURCE_KIND == 1"),
      TGFX_DECL_ELEMENT_ConditionalModule("pipeline/chain/pointwise_noise_uniforms.inc",
                                          "RESOURCE_KIND == 2"),
      TGFX_DECL_ELEMENT_ConditionalModule("pipeline/chain/pointwise_tiled2_uniforms.inc",
                                          "RESOURCE_KIND != 1"),
      TGFX_DECL_ELEMENT(Module, "xp/xp_uniforms.inc"),
  };

  static constexpr VaryingDecl Varyings[] = {
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
      {"vec2", "vStrokeWidth", 4, nullptr, "GP_LAYOUT == 4"},
      {"vec4", "vColor", 3, nullptr, "GP_LAYOUT >= 3"},
      // GP_LAYOUT 0: coverage and color follow the leaf coordinates.
      {"float", "vCoverage", 0, "NTEX", "GP_LAYOUT == 0 && HAS_COVERAGE"},
      {"vec4", "vColor", 0, "NTEX + 1", "GP_LAYOUT == 0"},
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

  // The leaf reader: one static sampler pick per instance, then the shared read swizzle. The
  // instance count is the sampler group's, so the two cannot drift.
  static constexpr InstanceDispatchDecl Dispatches[] = {
      {"aotChainRead", "TextureSampler_", "vec4", "vec2", "aotReadSwizzle", "NTEX > 0"},
  };

  static constexpr const char* PrologueIncludes[] = {
      "pipeline/chain/pointwise_fragment_config.inc",
      "pipeline/chain/pointwise_interface_layout.inc",
      // The generated leaf reader applies this decoder, so it must precede the reader.
      "source/texture_read_swizzle.inc",
  };

  static constexpr const char* Pragmas[] = {"tgfx_chain_ops", "tgfx_spec_include"};

  static constexpr const char* ExtraIncludes[] = {
      "pipeline/chain/pointwise_interface_support.inc",
      "pipeline/chain/pointwise_resource_eval.inc",
  };

  static constexpr FragmentFamilyDecl Decl = {Name,
                                              File,
                                              Block,
                                              std::size(Block),
                                              Varyings,
                                              std::size(Varyings),
                                              Samplers,
                                              std::size(Samplers),
                                              nullptr,
                                              0,
                                              nullptr,
                                              0,
                                              0,
                                              -1,
                                              "NTEX > 0",
                                              "NTEX",
                                              "(NTEX + 1)",
                                              Dispatches,
                                              std::size(Dispatches),
                                              "pipeline/chain/pointwise_fragment_main.inc",
                                              ExtraIncludes,
                                              std::size(ExtraIncludes),
                                              PrologueIncludes,
                                              std::size(PrologueIncludes),
                                              Pragmas,
                                              std::size(Pragmas)};
};

}  // namespace tgfx
