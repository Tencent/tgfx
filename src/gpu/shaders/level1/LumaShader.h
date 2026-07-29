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

/// Precompiled shader declaration for LumaFragmentProcessor. Extracts luminance from the input
/// color using configurable coefficients (default ITU-R BT.709: kr=0.2126, kg=0.7152, kb=0.0722).
///
/// Vertex dimensions:
///   GP_TYPE (int, 2 values): 0=DefaultGeometryProcessor, 1=QuadPerEdgeAAGeometryProcessor
///   HAS_COVERAGE (bool): per-vertex AA coverage attribute present. Driven by the GP's AAType and
///     kept independent of GP_TYPE (unified varying contract): both AA and non-AA quads share the
///     same GP_TYPE variant. Fixes the former GP_TYPE-coupled coverage that broke pipeline creation
///     for non-AA QuadPerEdgeAA luma draws.
class LumaShader : public PrecompiledShader {
 public:
  struct Dims {
    enum : uint32_t { GP_TYPE, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"LumaShader",
            "level1/luma.vert",
            "level1/luma.frag",
            Dims::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::LumaShader)
