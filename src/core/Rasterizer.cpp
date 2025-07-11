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

#include "Rasterizer.h"
#include "core/ShapeRasterizer.h"
#include "core/TextRasterizer.h"
#include "core/shapes/StrokeShape.h"

namespace tgfx {
std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(int width, int height,
                                                 std::shared_ptr<GlyphRunList> glyphRunList,
                                                 bool antiAlias, const Matrix& matrix,
                                                 const Stroke* stroke) {
  if (glyphRunList == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::make_shared<TextRasterizer>(width, height, std::move(glyphRunList), antiAlias, matrix,
                                          stroke);
}

std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(int width, int height, Path path, bool antiAlias,
                                                 const Matrix& matrix, const Stroke* stroke) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto shape = Shape::MakeFrom(std::move(path));
  shape = StrokeShape::Apply(std::move(shape), stroke, false);
  shape = Shape::ApplyMatrix(std::move(shape), matrix);
  if (shape == nullptr) {
    return nullptr;
  }
  auto aaType = antiAlias ? AAType::Coverage : AAType::None;
  return std::make_shared<ShapeRasterizer>(width, height, std::move(shape), aaType);
}

std::shared_ptr<Rasterizer> Rasterizer::MakeFrom(int width, int height,
                                                 std::shared_ptr<Shape> shape, bool antiAlias) {
  if (shape == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto aaType = antiAlias ? AAType::Coverage : AAType::None;
  return std::make_shared<ShapeRasterizer>(width, height, std::move(shape), aaType);
}

bool Rasterizer::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}
}  // namespace tgfx
