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

#include "GLSLClampedGradientEffect.h"
#include "gpu/ShaderCaps.h"

namespace tgfx {
// Mirrors the gradient layout ordering used by the precompiled gradient shaders'
PlacementPtr<ClampedGradientEffect> ClampedGradientEffect::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> colorizer,
    PlacementPtr<FragmentProcessor> gradLayout, Color leftBorderColor, Color rightBorderColor) {
  return allocator->make<GLSLClampedGradientEffect>(std::move(colorizer), std::move(gradLayout),
                                                    leftBorderColor, rightBorderColor);
}

GLSLClampedGradientEffect::GLSLClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                                                     PlacementPtr<FragmentProcessor> gradLayout,
                                                     Color leftBorderColor, Color rightBorderColor)
    : ClampedGradientEffect(std::move(colorizer), std::move(gradLayout), leftBorderColor,
                            rightBorderColor) {
}

void GLSLClampedGradientEffect::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("leftBorderColor", leftBorderColor);
  fragmentUniformData->setData("rightBorderColor", rightBorderColor);
  // Default for runtime-mask kernels; a present mask FP overwrites it with 1 later in traversal.
  fragmentUniformData->setDataOptional("HasDeviceMask", 0);
  // The precompiled gradient shaders fold the four layout maths into one variant selected by the
  // LayoutType uniform. Runtime-generated shaders emit layout-specific code and declare no such
  // field, so setDataOptional tolerates its absence there.
  fragmentUniformData->setDataOptional("LayoutType", ClampedGradientEffect::LayoutTypeIndex(
                                                         childProcessor(gradLayoutIndex)->name()));
}
}  // namespace tgfx
