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

/// Precompiled shader declaration for GaussianBlur1DFragmentProcessor. Performs a 1D Gaussian blur
/// by sampling a child texture in a loop. The loop upper bound is a fixed compile-time constant
/// (MAX_BLUR_SIGMA); the actual kernel radius is a runtime value taken from the Radius uniform,
/// and the loop breaks early once it is reached. The half-kernel weights are precomputed and
/// normalized on the CPU and uploaded via the shared Kernel vec4 array, matching the runtime
/// path's uniform contract bit-for-bit. The radius is therefore a runtime parameter, NOT a
/// compile-time permutation dimension — this keeps the variant count bounded (previously MAX_SIGMA
/// multiplied the whole fragment domain by 10).
///
/// Vertex dimensions:
///
/// Fragment dimensions:
///   (MAX_SIGMA removed — the radius is now a uniform, not a variant dimension)
///
/// Runtime uniforms: Kernel (vec4[KERNEL_VEC4_COUNT]), Radius (int), Step (vec2 direction vector)
class GaussianBlur1DShader : public ShaderFamily<GaussianBlur1DShader> {
 public:
  static constexpr const char* Name = "GaussianBlur1DShader";
  static constexpr const char* VertFile = "kernels/blur_texture.vert";
  static constexpr const char* FragFile = "kernels/gaussian_blur_1d.frag";

  struct VertDims {
    // No vertex dimensions: an empty domain, with no handles to keep aligned.
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // Child subset clamping is runtime: the shader always declares Subset and every tap clamps,
    // and a child without a real subset uploads the full texture bounds, so the clamp is a no-op.
    // The device mask and the tiled-child tap path are runtime uniforms (HasDeviceMask /
    // TiledChild), not permutation dimensions.
    // One typed handle per dimension; domain() lists them in the bundle's dimension order, and
    // Compose assigns through the handles by name (ComposedDims) — no index enum to keep
    // aligned with the domain list.
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = true;

  /// Matcher inputs: only the XferProcessor kind shapes the variant.
  struct Inputs {
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values. Pass-through: every input value lands on the
  /// domain one-to-one, so the mirrored Reachable default enumerates exactly what Compose emits.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE) {
      return std::nullopt;
    }
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.fragValues = frag.finish();
    return values;
  }

  /// Whole-draw guards (alpha-only target, GP kind, the blur child shape and the fixed kernel
  /// budget), defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::GaussianBlur1DShader)
