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

#include "GLSLGlassShapeGeometryFragmentProcessor.h"
#include <cmath>

namespace tgfx {

PlacementPtr<GlassSDFGeometryFragmentProcessor> GlassSDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, GlassShapeType shapeType, const GlassSDFGeometryParams& params) {
  if (allocator == nullptr || shapeType == GlassShapeType::AlphaMask) {
    return nullptr;
  }
  return allocator->make<GLSLGlassSDFGeometryFragmentProcessor>(shapeType, params);
}

GLSLGlassSDFGeometryFragmentProcessor::GLSLGlassSDFGeometryFragmentProcessor(
    GlassShapeType shapeType, const GlassSDFGeometryParams& params)
    : GlassSDFGeometryFragmentProcessor(shapeType, params) {
}

PlacementPtr<GlassUDFGeometryFragmentProcessor> GlassUDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> mask,
    std::shared_ptr<TextureProxy> edgeMask, const GlassUDFGeometryParams& params,
    bool enableEdgeLighting) {
  if (allocator == nullptr || mask == nullptr) {
    return nullptr;
  }
  if (enableEdgeLighting && edgeMask == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassUDFGeometryFragmentProcessor>(
      std::move(mask), std::move(edgeMask), params, enableEdgeLighting);
}

GLSLGlassUDFGeometryFragmentProcessor::GLSLGlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> mask, std::shared_ptr<TextureProxy> edgeMask,
    const GlassUDFGeometryParams& params, bool enableEdgeLighting)
    : GlassUDFGeometryFragmentProcessor(std::move(mask), std::move(edgeMask), params,
                                        enableEdgeLighting) {
}

}  // namespace tgfx
