/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause/
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// Precompiled shader declaration for GlassRefractionFragmentProcessor, bound to either
/// EllipseGeometryProcessor in its common-color form (the oval glass terminal draw) or
/// QuadPerEdgeAAGeometryProcessor in its common-color uvMatrix form (the rect-like glass terminal
/// draw, whose per-edge vertex coverage replaces the analytic ellipse coverage). That quad form
/// carries no uvCoord attribute: the matcher rejects the uvCoord form, so the vertex kernel
/// transforms the position instead. The geometry child
/// selects the GEOMETRY_KIND compile-time dimension because it changes the sampler layout: the SDF
/// kinds are purely procedural (source texture only), while the UDF kinds add the height mask and
/// the edge-light mask. DISPERSION_ON is a compile-time dimension for the same reason the runtime
/// makes it part of the program key (onComputeProcessorKey folds it into the key, and
/// GLSLGlassRefractionFragmentProcessor emits one path or the other): a runtime-uniform branch
/// would keep both paths in one text, and the compiler's cross-branch optimization then shifts the
/// sampled coordinate by an ULP relative to the runtime's branch-free program. Edge lighting still
/// rides a runtime uniform — it only adds to a colour already sampled, so it cannot move a texture
/// coordinate.
///
/// Vertex dimensions:
///   GP_KIND (int, 2 values): 0=Ellipse common color, 1=QuadPerEdgeAA (uvMatrix + edge coverage)
///
/// Fragment dimensions:
///   GP_KIND (int, 2 values): selects the initial-coverage source and the coordinate varying form
///   GEOMETRY_KIND (int, 4 values): 0=SDF rounded rect, 1=SDF ellipse, 2=UDF, 3=UDF + edge light
///   DISPERSION_ON (int, 2 values): 0=single tap, 1=three chromatic taps
///   HAS_XP (int, 3 values): XferProcessor type
class GlassRefractionShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_KIND", 2),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { GP_KIND, GEOMETRY_KIND, DISPERSION_ON, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_KIND", 2),
          PermutationInt("GEOMETRY_KIND", 4),
          PermutationInt("DISPERSION_ON", 2),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 4, "Update info() when fragment dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {"GlassRefractionShader",
            "level1/glass_refraction.vert",
            "level1/glass_refraction.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::GlassRefractionShader)
