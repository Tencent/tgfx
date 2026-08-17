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
///   (per-vertex coverage is unconditional: providers emit 1.0 for non-AA draws, so the
///    vCoverage varying is the only coverage path and HAS_COVERAGE no longer exists)
///   HAS_UV_COORD       (bool): explicit UV coordinate attribute present
///   (per-vertex color is unconditional: providers always write a color slot, broadcasting the
///    record's paint color for uniform-color batches)
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
    enum : uint32_t { HAS_UV_COORD, HAS_SUBSET, HAS_LOCAL_MASK, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_UV_COORD"),
          PermutationBool("HAS_SUBSET"),
          PermutationBool("HAS_LOCAL_MASK"),
      });
    }
  };
  using VD = VertDims;
  static_assert(VD::COUNT == 3, "Update ShouldCompile when vertex dimensions change.");

  // Fragment dimensions
  // ALPHA_ONLY and HAS_RGBAAA are intentionally NOT dimensions: both are pure fragment math
  // (alpha-only replicates .r; RGBAAA adds one coherent-branch alpha sample), so they are runtime
  // uniforms (AlphaOnly / HasRgbaaa) rather than compile-time permutations, shrinking the variants.
  // TEXTURE_KIND is the color texture's sampler type (0=TwoD, 1=Rect); it changes the sampler
  // declaration, so it is a legitimate dimension. It is appended last so existing TwoD variant
  // indices (TEXTURE_KIND=0) are unchanged. Rect variants compile only into the opengl bundle.
  struct FragDims {
    enum : uint32_t { HAS_SUBSET, HAS_XP, HAS_LOCAL_MASK, TEXTURE_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_SUBSET"),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_LOCAL_MASK"),
          PermutationEnum("TEXTURE_KIND", {"TwoD", "Rect"}),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 4, "Update ShouldCompile when fragment dimensions change.");

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
    // HAS_SUBSET / HAS_LOCAL_MASK vertex and fragment agreement is
    // enforced automatically by the framework (MirroredDimsAgree); the device mask is a runtime
    // uniform, so no mutual-exclusion carve-out is needed anymore.
    // Framebuffer fetch (HAS_XP=2) is Vulkan/Metal-only; Rect variants are opengl-only.
    return !(fragValues[FD::TEXTURE_KIND] == 1 && fragValues[FD::HAS_XP] == 2);
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::QuadTextureFillShader)
