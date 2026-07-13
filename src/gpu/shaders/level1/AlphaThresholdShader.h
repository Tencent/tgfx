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

/// Precompiled shader declaration for AlphaThresholdFragmentProcessor. Discards pixels whose alpha
/// falls below a uniform threshold by applying step(threshold, alpha).
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
class AlphaThresholdShader : public PrecompiledShader {
 public:
  struct Dims {
    enum : uint32_t { GP_TYPE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
      });
    }
  };

  struct FragDims {
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_XP"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"AlphaThresholdShader",
            "level1/alpha_threshold.vert",
            "level1/alpha_threshold.frag",
            Dims::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::AlphaThresholdShader)
