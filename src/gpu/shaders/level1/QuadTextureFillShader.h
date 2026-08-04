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
///   ALPHA_ONLY (bool): texture is alpha-only format
///   HAS_RGBAAA (bool): RGBAAA dual-plane alpha encoding
///   HAS_SUBSET (bool): subset clamping active
class QuadTextureFillShader : public PrecompiledShader {
 public:
  // Vertex dimensions
  struct VertDims {
    enum : uint32_t { HAS_COVERAGE, HAS_UV_COORD, HAS_COLOR, HAS_SUBSET, HAS_LOCAL_MASK, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_COLOR"),
          PermutationBool("HAS_SUBSET"),
          PermutationBool("HAS_LOCAL_MASK"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 5, "Update ShouldCompile when vertex dimensions change.");

  // Fragment dimensions (includes vertex-driven HAS_COVERAGE and HAS_COLOR because the fragment
  // shader must declare matching varyings)
  // ALPHA_ONLY and HAS_RGBAAA are intentionally NOT dimensions: both are pure fragment math
  // (alpha-only replicates .r; RGBAAA adds one coherent-branch alpha sample), so they are runtime
  // uniforms (AlphaOnly / HasRgbaaa) rather than compile-time permutations, shrinking the variants.
  struct FragDims {
    enum : uint32_t {
      HAS_SUBSET,
      HAS_COVERAGE,
      HAS_COLOR,
      HAS_XP,
      HAS_MASK_TEXTURE,
      HAS_LOCAL_MASK,
      COUNT
    };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_SUBSET"),
          PermutationBool("HAS_COVERAGE"),
          PermutationBool("HAS_COLOR"),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_MASK_TEXTURE"),
          PermutationBool("HAS_LOCAL_MASK"),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 6, "Update ShouldCompile when fragment dimensions change.");

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
  static bool ShouldCompile(uint32_t, uint32_t, const std::vector<int>&,
                            const std::vector<int>& fragValues) {
    // HAS_SUBSET / HAS_COVERAGE / HAS_COLOR / HAS_LOCAL_MASK vertex and fragment agreement is
    // enforced automatically by the framework (MirroredDimsAgree).
    if (fragValues[FD::HAS_LOCAL_MASK] != 0) {
      // Local-space and device-space masks are mutually exclusive coverage sources.
      if (fragValues[FD::HAS_MASK_TEXTURE] != 0) {
        return false;
      }
      // The local-mask coverage class (Xfermode-dst(TextureEffect)) is produced by image draws with
      // a shader mask filter: the color is a single TextureEffect with no per-vertex color. Restrict
      // to that slice so the new dimension adds a bounded number of variants.
      if (fragValues[FD::HAS_COLOR] != 0) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadTextureFillShader)
