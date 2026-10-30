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

class FragmentProcessor;
class ProgramInfo;

class YUVTextureFillShader : public ShaderFamily<YUVTextureFillShader> {
 public:
  static constexpr const char* Name = "YUVTextureFillShader";
  static constexpr const char* VertFile = "kernels/yuv_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/yuv_texture_fill.frag";

  // The one geometry processor this family accepts: reported by info() and compared by Extract.
  static constexpr const char* GpClassName = "QuadPerEdgeAAGeometryProcessor";

  struct VertDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationBoolDim<VertDims> HAS_UV_COORD{"HAS_UV_COORD"};
    static PermutationDomain domain() {
      return PermutationDomain({HAS_UV_COORD});
    }
  };
  using VD = VertDims;

  struct FragDims {
    // One typed handle per dimension; domain() in bundle order (see ShaderPermutation.h).
    static inline const PermutationIntDim<FragDims> YUV_FORMAT{"YUV_FORMAT", 2};
    static inline const PermutationIntDim<FragDims> HAS_XP{XP_DEFINE_NAME, XP_VALUE_COUNT};
    static PermutationDomain domain() {
      return PermutationDomain({YUV_FORMAT, HAS_XP});
    }
  };
  using FD = FragDims;
  /// Whether alpha-only (AAAA) render targets may be served by this kernel.
  static constexpr bool ServesAlphaOnly = false;

  /// Matcher inputs: the UV-matrix form (the vert domain stores the inverted flag), the plane
  /// format, and the XferProcessor kind.
  struct Inputs {
    bool hasUVMatrix = false;
    int yuvFormat = -1;
    int xpType = XP_UNSUPPORTED;
  };

  /// Pure mapping from inputs to dimension values.
  static std::optional<RuleComposedValues> Compose(const Inputs& inputs) {
    if (inputs.xpType < XP_NONE || inputs.yuvFormat < 0) {
      return std::nullopt;
    }
    auto vert = ComposedDims<VertDims>(VertDims::domain());
    vert.set(VD::HAS_UV_COORD, inputs.hasUVMatrix ? 0 : 1);
    auto frag = ComposedDims<FragDims>(FragDims::domain());
    frag.set(FD::YUV_FORMAT, inputs.yuvFormat);
    frag.set(FD::HAS_XP, inputs.xpType);
    RuleComposedValues values;
    values.vertValues = vert.finish();
    values.fragValues = frag.finish();
    return values;
  }

  /// The enumerable input space the Reachable set maps through Compose (both plane formats, both
  /// UV forms, every XP kind).
  static std::vector<Inputs> AllInputs() {
    std::vector<Inputs> result;
    for (int hasUVMatrix = 0; hasUVMatrix <= 1; ++hasUVMatrix) {
      for (int yuvFormat = 0; yuvFormat <= 1; ++yuvFormat) {
        for (int xpType = XP_NONE; xpType <= XP_FRAMEBUFFER_FETCH; ++xpType) {
          result.push_back(Inputs{hasUVMatrix != 0, yuvFormat, xpType});
        }
      }
    }
    return result;
  }

  /// Whole-draw guards (alpha-only target, geometry processor kind, the YUV source shape),
  /// defined in the family's own .cpp, next to the GLSL it owns.
  static std::optional<Inputs> Extract(const ProgramInfo* programInfo,
                                       PermutationMatchFailure* failure);
};

}  // namespace tgfx

TGFX_REGISTER_SHADER_FAMILY(tgfx::YUVTextureFillShader)
