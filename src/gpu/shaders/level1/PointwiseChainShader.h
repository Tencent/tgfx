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

/// Precompiled shader that evaluates an arbitrary pointwise DAG (texture and const-color leaves
/// combined by color-matrix, luma, alpha-threshold, color-space-xform and blend ops) in a single
/// fused pass. The DAG shape is runtime data: every node occupies one of 16 statically expanded
/// slots whose OpType and two input-slot indices are uniforms, so any topology hits the same
/// variant. Only the texture-leaf count is a compile-time dimension, because each leaf adds a
/// sampler binding and a TransformedCoords varying.
class PointwiseChainShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { GP_TYPE, HAS_COVERAGE, TEXTURE_COUNT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("GP_TYPE", 2),
          PermutationBool("HAS_COVERAGE"),
          // Encodes the texture-leaf count: 0 -> 1 leaf, 1 -> 2 leaves, 2 -> 4 leaves. These are
          // the only counts the decomposition planner produces; the vertex stage must know it
          // because each leaf adds one TransformedCoords varying to the interface.
          PermutationInt("TEXTURE_COUNT", 3),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_COVERAGE, TEXTURE_COUNT, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_COVERAGE"),
          PermutationInt("TEXTURE_COUNT", 3),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"PointwiseChainShader",
            "level1/pointwise_chain.vert",
            "level1/pointwise_chain.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseChainShader)
