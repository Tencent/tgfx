/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/PathRasterizer.h"
#include "core/ScalerContext.h"

namespace tgfx {
std::shared_ptr<PathRasterizer> PathRasterizer::MakeFrom(int width, int height, Path path,
                                                         bool antiAlias, const Matrix* matrix,
                                                         bool needsGammaCorrection) {
  auto shape = Shape::MakeFrom(std::move(path));
  if (matrix != nullptr) {
    shape = Shape::ApplyMatrix(std::move(shape), *matrix);
  }
  if (shape == nullptr) {
    return nullptr;
  }
  return MakeFrom(width, height, std::move(shape), antiAlias, needsGammaCorrection);
}

PathRasterizer::PathRasterizer(int width, int height, std::shared_ptr<Shape> shape, bool antiAlias,
                               bool needsGammaCorrection)
    : ImageCodec(width, height), shape(std::move(shape)), antiAlias(antiAlias),
      needsGammaCorrection(needsGammaCorrection) {
}

bool PathRasterizer::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}
}  //namespace tgfx
