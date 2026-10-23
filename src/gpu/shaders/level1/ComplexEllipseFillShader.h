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

class ComplexEllipseFillShader : public PrecompiledShader {
 public:
  // HAS_COMMON_COLOR is not a dimension: the color attribute is unconditional (common-color
  // batches broadcast the paint color), and the color source is the HasCommonColor runtime
  // uniform.
  TGFX_DEFINE_DIMS(STROKE);
  using D = Dims;

  struct FragDims {
    // The device-space mask is a runtime uniform (HasDeviceMask) with a padded dummy sampler: this
    // family is rare, and a HAS_DEVICE_MASK dimension would double all stroke/XP variants for it.
    enum : uint32_t { STROKE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("STROKE"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 1 && FD::COUNT == 2,
                "Update the Compose mapping when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationMatchResult> Match(const ProgramInfo* programInfo);

  PrecompiledShaderInfo info() const override {
    return {"ComplexEllipseFillShader",
            "level1/complex_ellipse_fill.vert",
            "level1/complex_ellipse_fill.frag",
            D::domain(),
            FD::domain(),
            PermutationDomain({}),
            "ComplexEllipseGeometryProcessor",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::ComplexEllipseFillShader, tgfx::EnumerateComplexEllipseFillReachable)
