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
struct PermutationVariant;

class MeshFillShader : public PrecompiledShader {
 public:
  // HAS_COLOR and HAS_COVERAGE are not dimensions: the color and coverage attributes are
  // unconditional (white baked for meshes without per-vertex colors, a constant 1.0f coverage),
  // and colorless draws pick the exact Color uniform through the HasCommonColor runtime uniform.
  TGFX_DEFINE_DIMS(HAS_TEX_COORDS);
  using VD = Dims;

  struct FragDims {
    // HAS_TEX_COORDS stays vertex-only: the fragment stage never branches on it (mesh_fill.frag
    // does not reference the define), so it is not part of the fragment domain.
    enum : uint32_t { HAS_XP, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt(XP_DEFINE_NAME, XP_VALUE_COUNT),
      });
    }
  };
  using FD = FragDims;
  static_assert(VD::COUNT == 1 && FD::COUNT == 1,
                "Update the Compose mapping when dimensions change.");

  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Runtime matching: does this draw belong to this family? Declared here, defined in
  /// the family's own .cpp, next to the GLSL it owns.
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo);

  // The one geometry processor this family accepts: info() reports it, Match compares against it.
  static constexpr const char* GpClassName = "MeshGeometryProcessor";
  PrecompiledShaderInfo info() const override {
    return {"MeshFillShader",
            "level1/mesh_fill.vert",
            "level1/mesh_fill.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            GpClassName,
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::MeshFillShader, tgfx::EnumerateMeshFillReachable)
