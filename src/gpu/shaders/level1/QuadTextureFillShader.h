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
///   (Perspective is handled uniformly: the coord is always emitted as vec3 and divided in the
///    fragment shader; affine transforms yield z=1 so the divide is a no-op.)
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
    enum : uint32_t { HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, HAS_SUBSET, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
          PermutationBool("HAS_SUBSET"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 4, "Update ShouldCompile when vertex dimensions change.");

  // Fragment dimensions (includes vertex-driven HAS_COVERAGE and HAS_COLOR because the fragment
  // shader must declare matching varyings)
  // ALPHA_ONLY is intentionally NOT a dimension: it is pure fragment math (replicate .r, scale by
  // alpha), so it is a runtime uniform (AlphaOnly) rather than a compile-time permutation. This
  // halves the fragment variant count.
  struct FragDims {
    enum : uint32_t {
      HAS_YUV,
      HAS_RGBAAA,
      HAS_SUBSET,
      HAS_COVERAGE,
      HAS_COLOR,
      HAS_XP,
      HAS_MASK_TEXTURE,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_YUV"),
          PermutationBool("HAS_RGBAAA"),
          PermutationBool("HAS_SUBSET"),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_MASK_TEXTURE"),
      });
    }
  };
  using FD = FragDims;
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
    // When vertex has the subset attribute, fragment uses HAS_SUBSET=1 (per-quad varying + uniform
    // double clamp). When it lacks it, fragment uses HAS_SUBSET=0 and always clamps by the Subset
    // uniform alone; the uniform is populated with full texture bounds when no real subset applies,
    // making that clamp a no-op.
    if (vertValues[VD::HAS_SUBSET] != fragValues[FD::HAS_SUBSET]) {
      return false;
    }
    // HAS_COVERAGE, HAS_COLOR in frag must match vert (mirrored dimensions).
    if (vertValues[VD::HAS_COVERAGE] != fragValues[FD::HAS_COVERAGE]) {
      return false;
    }
    if (vertValues[VD::HAS_COLOR] != fragValues[FD::HAS_COLOR]) {
      return false;
    }
    // A device-space mask (HAS_MASK_TEXTURE) is a clip/layer coverage applied to a plain textured
    // fill. It does not co-occur with RGBAAA (packed alpha) sources, so prune that cartesian branch
    // to keep the variant count bounded — adding the mask dimension would otherwise double the
    // whole dimension product.
    if (fragValues[FD::HAS_MASK_TEXTURE] != 0 && fragValues[FD::HAS_RGBAAA] != 0) {
      return false;
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadTextureFillShader)
