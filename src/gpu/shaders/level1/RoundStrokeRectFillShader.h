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
struct PermutationVariant;

class RoundStrokeRectFillShader : public PrecompiledShader {
 public:
  // The color attribute is unconditional (common-color batches broadcast the paint color; the
  // exact color rides the Color uniform through the HasCommonColor runtime uniform), so there is
  // no HAS_COMMON_COLOR dimension. The coverage and ellipse-radii attributes are unconditional
  // too (non-AA draws write a 1.0 coverage and zero radii), with the AA ramp selected by the
  // AAEnabled runtime uniform, so there is no HAS_AA dimension either.
  TGFX_DEFINE_DIMS(HAS_UV_MATRIX);
  using VD = Dims;

  struct FragDims {
    // HAS_UV_MATRIX stays vertex-only: the fragment stage never branches on it
    // (round_stroke_rect_fill.frag does not reference the define), so it is not part of the
    // fragment domain.
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 1 && FD::COUNT == 1,
                "Update the Compose mapping when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"RoundStrokeRectFillShader",
            "level1/round_stroke_rect_fill.vert",
            "level1/round_stroke_rect_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "RoundStrokeRectGeometryProcessor",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::RoundStrokeRectFillShader, tgfx::EnumerateRoundStrokeRectReachable)
