/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <vector>
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {

class ProgramInfo;

class ShapeInstancedTextureCoverageShader
    : public ShaderFamily<ShapeInstancedTextureCoverageShader> {
 public:
  static constexpr const char* Name = "ShapeInstancedTextureCoverageShader";
  static constexpr const char* VertFile = "kernels/shape_instanced_texture_coverage.vert";
  static constexpr const char* FragFile = "kernels/shape_instanced_texture_coverage.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "ShapeInstancedGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> GRADIENT{"GRADIENT"};
    static PermutationDomain domain() {
      return PermutationDomain({GRADIENT});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<FragDims> GRADIENT{"GRADIENT"};
    static PermutationDomain domain() {
      return PermutationDomain({GRADIENT});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Matcher inputs: the gradient presence ahead of the coverage texture.
  struct Inputs {
    bool gradient = false;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domains one-to-one (GRADIENT mirrors to both stages), so the mirrored Reachable default
  /// enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    int gradient = inputs.gradient ? 1 : 0;
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::GRADIENT, gradient);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::GRADIENT, gradient);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the bare or gradient-topped
  /// coverage texture shape), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::ShapeInstancedTextureCoverageShader)
