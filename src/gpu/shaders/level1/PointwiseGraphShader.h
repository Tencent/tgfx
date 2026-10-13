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

#include <utility>
#include "gpu/shaders/level1/PointwiseChainShader.h"

namespace tgfx {

// Single-pass extended pointwise graphs sharing the chain instruction ABI and GP layouts.
// Resource kind 0 retains the fixed noise-quad subset; kind 1 supports one gradient and three
// tiled-image recipes throughout the chain's reachable layouts.
class PointwiseGraphShader : public PrecompiledShader {
 public:
  using VD = PointwiseChainShader::VD;

  struct FragDims : PointwiseChainShader::FD {
    enum : uint32_t { RESOURCE_KIND = PointwiseChainShader::FD::COUNT, COUNT };
    static PermutationDomain domain() {
      auto dimensions = PointwiseChainShader::FD::domain().getDimensions();
      dimensions.emplace_back(PermutationInt("RESOURCE_KIND", 2));
      return PermutationDomain(std::move(dimensions));
    }
  };
  using FD = FragDims;
  static_assert(FD::COUNT == 7, "Update the Compose mapping when fragment dimensions change.");

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
