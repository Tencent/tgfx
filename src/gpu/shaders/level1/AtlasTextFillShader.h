/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

class ProgramInfo;
struct PermutationMatchResult;

class AtlasTextFillShader : public PrecompiledShader {
 public:
  // ALPHA_ONLY is a runtime uniform (AlphaOnly), written by GLSLAtlasTextGeometryProcessor::setData,
  // not a permutation dimension — the vertex stage never uses it.
  // HAS_COMMON_COLOR is not a dimension either: the color attribute is unconditional (common-color
  // glyph batches broadcast the paint color), and the color source is the HasCommonColor runtime
  // uniform.
  TGFX_DEFINE_DIMS(HAS_COVERAGE);
  using D = Dims;

  struct FragDims {
    // A device-space mask is a runtime uniform (HasDeviceMask), not a permutation dimension.
    enum : uint32_t { HAS_COVERAGE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 1 && FD::COUNT == 2,
                "Update the Compose mapping when dimensions change.");

 public:
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationMatchResult> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"AtlasTextFillShader",
            "level1/atlas_text_fill.vert",
            "level1/atlas_text_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AtlasTextFillShader, tgfx::EnumerateAtlasTextFillReachable)
