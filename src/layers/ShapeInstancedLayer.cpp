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

#include "tgfx/layers/ShapeInstancedLayer.h"
#include <cstring>
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

std::shared_ptr<ShapeInstancedLayer> ShapeInstancedLayer::Make() {
  return std::shared_ptr<ShapeInstancedLayer>(new ShapeInstancedLayer());
}

void ShapeInstancedLayer::setShape(std::shared_ptr<Shape> shape) {
  if (_shape == shape) {
    return;
  }
  _shape = std::move(shape);
  invalidateContent();
}

void ShapeInstancedLayer::setMatrices(std::vector<Matrix> matrices) {
  // Use memcmp instead of operator== for faster comparison on large arrays. Although Matrix is not
  // a POD type (it has a mutable typeMask cache field), the worst case of memcmp mismatch is an
  // extra invalidation, which is acceptable for the performance gain in dynamic instancing scenes.
  if (_matrices.size() == matrices.size() &&
      memcmp(_matrices.data(), matrices.data(), sizeof(Matrix) * matrices.size()) == 0) {
    return;
  }
  _matrices = std::move(matrices);
  invalidateContent();
}

void ShapeInstancedLayer::setColors(std::vector<Color> colors) {
  if (_colors.size() == colors.size() &&
      memcmp(_colors.data(), colors.data(), sizeof(Color) * colors.size()) == 0) {
    return;
  }
  _colors = std::move(colors);
  invalidateContent();
}

void ShapeInstancedLayer::setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills) {
  if (_fillStyles.size() == fills.size() &&
      std::equal(_fillStyles.begin(), _fillStyles.end(), fills.begin())) {
    return;
  }
  _fillStyles = std::move(fills);
  invalidateContent();
}

void ShapeInstancedLayer::removeFillStyles() {
  if (_fillStyles.empty()) {
    return;
  }
  _fillStyles = {};
  invalidateContent();
}

void ShapeInstancedLayer::setFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    removeFillStyles();
  } else {
    setFillStyles({std::move(fillStyle)});
  }
}

void ShapeInstancedLayer::addFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    return;
  }
  _fillStyles.push_back(std::move(fillStyle));
  invalidateContent();
}

void ShapeInstancedLayer::onUpdateContent(LayerRecorder* recorder) {
  if (_shape == nullptr || _matrices.empty()) {
    return;
  }

  if (_fillStyles.empty()) {
    recorder->addShapeInstanced(_shape, _matrices, _colors, LayerPaint(Color::White()));
  } else {
    for (const auto& style : _fillStyles) {
      LayerPaint paint(style->color(), style->blendMode());
      paint.shader = style->shader();
      recorder->addShapeInstanced(_shape, _matrices, _colors, paint);
    }
  }
}

}  // namespace tgfx
