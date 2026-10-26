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

class EllipseFillShader : public PrecompiledShader {
 public:
  // HAS_COMMON_COLOR is not a dimension: the color attribute is unconditional (common-color
  // batches broadcast the paint color), and the color source is the HasCommonColor runtime
  // uniform.
  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_DEVICE_MASK, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
          PermutationBool("HAS_DEVICE_MASK"),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 0 && FD::COUNT == 2,
                "Update the matcher below when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo);

  // The one geometry processor this family accepts: info() reports it, Match compares against it.
  static constexpr const char* GpClassName = "EllipseGeometryProcessor";
  PrecompiledShaderInfo info() const override {
    return {"EllipseFillShader",
            "level1/ellipse_fill.vert",
            "level1/ellipse_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            GpClassName,
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::EllipseFillShader, tgfx::EnumerateEllipseFillReachable)
