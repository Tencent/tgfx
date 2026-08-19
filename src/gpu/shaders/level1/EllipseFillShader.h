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

class EllipseFillShader : public PrecompiledShader {
 public:
  TGFX_DEFINE_DIMS(HAS_COMMON_COLOR);
  using D = Dims;

  struct FragDims {
    enum : uint32_t { HAS_COMMON_COLOR, HAS_XP, HAS_DEVICE_MASK, COUNT };
    static PermutationDomain domain() {
      return PermutationDomain({
          PermutationBool("HAS_COMMON_COLOR"),
          PermutationInt("HAS_XP", 3),
          PermutationBool("HAS_DEVICE_MASK"),
      });
    }
  };
  using FD = FragDims;
  static_assert(D::COUNT == 1 && FD::COUNT == 3,
                "Update the matcher below when dimensions change.");

  PrecompiledShaderInfo info() const override {
    return {
        "EllipseFillShader", "level1/ellipse_fill.vert", "level1/ellipse_fill.frag", D::domain(),
        FD::domain(),        PermutationDomain({}),      "EllipseGeometryProcessor", ""};
  }
};

}  // namespace tgfx

TGFX_REGISTER_SHADER(tgfx::EllipseFillShader)
