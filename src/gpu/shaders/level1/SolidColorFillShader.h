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

/// Precompiled shader for a solid-color fill through DefaultGeometryProcessor with no fragment
/// processors. The fill color comes from the geometry processor's Color uniform. Covers the common
/// plain drawRect/drawPath path (no shader, no color/coverage FP). The coverage attribute is
/// unconditional (the non-AA path triangles carry a constant 1.0 slot), so there is no
/// HAS_COVERAGE dimension.
///
/// Fragment dimensions:
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF.
class SolidColorFillShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 1, "Update Match in SolidColorFillShader.cpp when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = true;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in the
  /// family's own .cpp (SolidColorFillShader.cpp) next to the GLSL it owns.
  static std::optional<PermutationMatchResult> Match(const ProgramInfo* programInfo);

  // The one geometry processor this family accepts: info() reports it, Match compares against it.
  static constexpr const char* GpClassName = "DefaultGeometryProcessor";
  PrecompiledShaderInfo info() const override {
    return {"SolidColorFillShader",
            "level1/solid_color_fill.vert",
            "level1/solid_color_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            GpClassName,
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::SolidColorFillShader, tgfx::EnumerateSolidColorFillReachable)
