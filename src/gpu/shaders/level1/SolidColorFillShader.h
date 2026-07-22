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

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Precompiled shader for a solid-color fill through DefaultGeometryProcessor with no fragment
/// processors. The fill color comes from the geometry processor's Color uniform. Covers the common
/// plain drawRect/drawPath path (no shader, no color/coverage FP).
///
/// Vertex dimensions:
///   HAS_COVERAGE (bool): DefaultGP has per-vertex AA coverage (AAType::Coverage), adding the
///                        inCoverage attribute and vCoverage varying.
///
/// Fragment dimensions:
///   HAS_COVERAGE (bool): mirrors the vertex dimension (varying declarations must agree).
///   HAS_XP (int, 3 values): 0=Empty, 1=PorterDuff DST_TEX, 2=PorterDuff FBF.
class SolidColorFillShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_COVERAGE, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 2, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"SolidColorFillShader",
            "level1/solid_color_fill.vert",
            "level1/solid_color_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            ShouldCompile};
  }

 private:
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>& vertValues,
                            const std::vector<int>& fragValues) {
    // The coverage varying declarations must agree between vertex and fragment stages.
    return vertValues[VD::HAS_COVERAGE] == fragValues[FD::HAS_COVERAGE];
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::SolidColorFillShader)
