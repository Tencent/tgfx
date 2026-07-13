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

/// Precompiled shader for QuadPerEdgeAAGeometryProcessor + TextureEffect pipeline.
/// Covers the common drawImage path where vertices are pre-transformed to device space.
///
/// Vertex dimensions (driven by QuadPerEdgeAAGeometryProcessor configuration):
///   HAS_COVERAGE       (bool): AA coverage attribute present
///   HAS_UV_COORD       (bool): explicit UV coordinate attribute present
///   HAS_COLOR          (bool): per-vertex color attribute present
///   HAS_SUBSET         (bool): texture subset attribute present
///   HAS_UV_PERSPECTIVE (bool): UV transform includes perspective
///
/// Fragment dimensions (same semantics as TextureFillShader):
///   HAS_YUV    (bool): YUV texture (not compiled — falls back to ProgramBuilder)
///   ALPHA_ONLY (bool): texture is alpha-only format
///   HAS_RGBAAA (bool): RGBAAA dual-plane alpha encoding
///   HAS_SUBSET (bool): subset clamping active
class QuadTextureFillShader : public PrecompiledShader {
 public:
  // Vertex dimensions
  struct VertDims {
    enum : uint32_t {
      HAS_COVERAGE,
      HAS_UV_COORD,
      HAS_COLOR,
      HAS_SUBSET,
      HAS_UV_PERSPECTIVE,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
          PermutationBool("HAS_SUBSET"),
          PermutationBool("HAS_UV_PERSPECTIVE"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 5, "Update ShouldCompile when vertex dimensions change.");

  // Fragment dimensions (includes vertex-driven HAS_COVERAGE and HAS_COLOR because the fragment
  // shader must declare matching varyings and apply coverage/color logic accordingly)
  TGFX_DEFINE_DIMS(HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET, HAS_COVERAGE, HAS_COLOR, HAS_XP);
  using FD = Dims;
  static_assert(FD::COUNT == 7, "Update ShouldCompile when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"QuadTextureFillShader",
            "level1/quad_texture_fill.vert",
            "level1/quad_texture_fill.frag",
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
    // YUV textures require additional dimensions not yet modeled.
    if (fragValues[FD::HAS_YUV] != 0) {
      return false;
    }
    // ALPHA_ONLY and HAS_RGBAAA are mutually exclusive in practice.
    if (fragValues[FD::ALPHA_ONLY] != 0 && fragValues[FD::HAS_RGBAAA] != 0) {
      return false;
    }
    // HAS_COVERAGE and HAS_COLOR in frag must match vert (they are mirrored dimensions).
    if (vertValues[VD::HAS_COVERAGE] != fragValues[FD::HAS_COVERAGE]) {
      return false;
    }
    if (vertValues[VD::HAS_COLOR] != fragValues[FD::HAS_COLOR]) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadTextureFillShader)
