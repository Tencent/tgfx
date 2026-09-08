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
/// QuadPerEdgeAAGeometryProcessor in its common-color uvCoord form (the rect-like glass terminal
/// draw, whose per-edge vertex coverage replaces the analytic ellipse coverage). The geometry child
/// selects the GEOMETRY_KIND compile-time dimension because it changes the sampler layout: the SDF
/// kinds are purely procedural (source texture only), while the UDF kinds add the height mask and
/// the edge-light mask. Dispersion and edge lighting ride runtime-uniform branches that mirror the
/// runtime emission's static branches expression-for-expression, so they add no variants.
///
/// Vertex dimensions:
///   GP_KIND (int, 2 values): 0=Ellipse common color, 1=QuadPerEdgeAA (uvCoord + edge coverage)
///
/// Fragment dimensions:
///   GP_KIND (int, 2 values): selects the initial-coverage source and the coordinate varying form
///   GEOMETRY_KIND (int, 4 values): 0=SDF rounded rect, 1=SDF ellipse, 2=UDF, 3=UDF + edge light
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
    enum : uint32_t { GP_KIND, GEOMETRY_KIND, HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_KIND", 2),
          PermutationInt("GEOMETRY_KIND", 4),
          PermutationInt("HAS_XP", 3),
      });
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 3, "Update info() when fragment dimensions change.");

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
