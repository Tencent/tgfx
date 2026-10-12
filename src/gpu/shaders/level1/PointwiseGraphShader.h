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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

// Single-pass extended pointwise graphs. Shares the chain instruction ABI and evaluator;
// resource kind 0 carries one Perlin computation, kind 1 carries three tiled-image recipes.
// Both profiles use common-color, position-based AA quads with four leaf bindings.
class PointwiseGraphShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { COUNT = 0 };
    static PermutationDomain domain() {
      return PermutationDomain({});
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, RESOURCE_KIND, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({PermutationInt("HAS_XP", 3), PermutationInt("RESOURCE_KIND", 2)});
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"PointwiseGraphShader",
            "level1/pointwise_graph.vert",
            "level1/pointwise_graph.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::PointwiseGraphShader)
