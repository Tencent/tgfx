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
//  unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLGlassRefractionFragmentProcessor.h"
#include <cmath>

namespace tgfx {

PlacementPtr<GlassRefractionFragmentProcessor> GlassRefractionFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix) {
  if (allocator == nullptr || source == nullptr || geometry == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(
      std::move(source), std::move(geometry), params, coordMatrix);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(geometry), params,
                                       coordMatrix) {
}

}  // namespace tgfx
