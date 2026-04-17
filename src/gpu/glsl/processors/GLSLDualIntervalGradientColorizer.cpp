/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLDualIntervalGradientColorizer.h"

namespace tgfx {
PlacementPtr<DualIntervalGradientColorizer> DualIntervalGradientColorizer::Make(
    BlockAllocator* allocator, Color c0, Color c1, Color c2, Color c3, float threshold) {
  Color scale01;
  // Derive scale and biases from the 4 colors and threshold
  for (int i = 0; i < 4; ++i) {
    auto vc0 = c0[i];
    auto vc1 = c1[i];
    scale01[i] = (vc1 - vc0) / threshold;
  }
  // bias01 = c0

  Color scale23;
  Color bias23;
  for (int i = 0; i < 4; ++i) {
    auto vc2 = c2[i];
    auto vc3 = c3[i];
    scale23[i] = (vc3 - vc2) / (1 - threshold);
    bias23[i] = vc2 - threshold * scale23[i];
  }

  return allocator->make<GLSLDualIntervalGradientColorizer>(scale01, c0, scale23, bias23,
                                                            threshold);
}

GLSLDualIntervalGradientColorizer::GLSLDualIntervalGradientColorizer(Color scale01, Color bias01,
                                                                     Color scale23, Color bias23,
                                                                     float threshold)
    : DualIntervalGradientColorizer(scale01, bias01, scale23, bias23, threshold) {
}

void GLSLDualIntervalGradientColorizer::onSetData(UniformData* /*vertexUniformData*/,
                                                  UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("scale01", scale01);
  fragmentUniformData->setData("bias01", bias01);
  fragmentUniformData->setData("scale23", scale23);
  fragmentUniformData->setData("bias23", bias23);
  fragmentUniformData->setData("threshold", threshold);
}
}  // namespace tgfx
