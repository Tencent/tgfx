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

class NonAARRectFillShader : public PrecompiledShader {
 public:
  // The color attribute is unconditional (common-color batches broadcast the paint color; the
  // exact color rides the Color uniform through the HasCommonColor runtime uniform), so there is
  // no HAS_COMMON_COLOR dimension.
  TGFX_DEFINE_DIMS(STROKE, TEXTURED);
  using D = Dims;

  struct FragDims {
    enum : uint32_t { STROKE, TEXTURED, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("STROKE"),
          PermutationBool("TEXTURED"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 2 && FD::COUNT == 3,
                "Update the Compose mapping when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationMatchResult> Match(const ProgramInfo* programInfo);

  // The one geometry processor this family accepts: info() reports it, Match compares against it.
  static constexpr const char* GpClassName = "NonAARRectGeometryProcessor";
  PrecompiledShaderInfo info() const override {
    return {"NonAARRectFillShader",
            "level1/non_aa_rrect_fill.vert",
            "level1/non_aa_rrect_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            GpClassName,
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::NonAARRectFillShader, tgfx::EnumerateNonAARRectFillReachable)
