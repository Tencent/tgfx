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

class SingleIntervalGradientShader : public PrecompiledShader {
 public:
  struct VertDims {
    enum : uint32_t { HAS_VCOVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_VCOVERAGE"),
      });
    }
  };
  using VD = VertDims;

  struct FragDims {
    enum : uint32_t { HAS_XP, HAS_DEVICE_MASK, HAS_VCOVERAGE, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_DEVICE_MASK"),
          PermutationBool("HAS_VCOVERAGE"),
      });
    }
  };
  using FD = FragDims;

  PrecompiledShaderInfo info() const override {
    return {"SingleIntervalGradientShader",
            "level1/gradient_fill.vert",
            "level1/single_interval_gradient.frag",
            VD::domain(),
            FD::domain(),
            PermutationDomain({}),
            "",
            "",
            nullptr};
  }

 private:
  // HAS_VCOVERAGE is mirrored across stages: the vertex shader emits the coverage varying only when
  // the fragment shader consumes it. The GP difference is absorbed by the Matrix uniform — the fragment stage is
  // identical for all GP types, so it is not a fragment dimension.
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::SingleIntervalGradientShader)
