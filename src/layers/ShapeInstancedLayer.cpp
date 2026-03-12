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
#include "ShapeStrokeEffect.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

std::shared_ptr<ShapeInstancedLayer> ShapeInstancedLayer::Make() {
  return std::shared_ptr<ShapeInstancedLayer>(new ShapeInstancedLayer());
}

ShapeInstancedLayer::ShapeInstancedLayer() {
  memset(&shapeBitFields, 0, sizeof(shapeBitFields));
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

void ShapeInstancedLayer::setStrokeStyles(std::vector<std::shared_ptr<ShapeStyle>> strokes) {
  if (_strokeStyles.size() == strokes.size() &&
      std::equal(_strokeStyles.begin(), _strokeStyles.end(), strokes.begin())) {
    return;
  }
  _strokeStyles = std::move(strokes);
  invalidateContent();
}

void ShapeInstancedLayer::removeStrokeStyles() {
  if (_strokeStyles.empty()) {
    return;
  }
  _strokeStyles = {};
  invalidateContent();
}

void ShapeInstancedLayer::setStrokeStyle(std::shared_ptr<ShapeStyle> strokeStyle) {
  if (strokeStyle == nullptr) {
    removeStrokeStyles();
  } else {
    setStrokeStyles({std::move(strokeStyle)});
  }
}

void ShapeInstancedLayer::addStrokeStyle(std::shared_ptr<ShapeStyle> strokeStyle) {
  if (strokeStyle == nullptr) {
    return;
  }
  _strokeStyles.push_back(std::move(strokeStyle));
  invalidateContent();
}

void ShapeInstancedLayer::setLineCap(LineCap cap) {
  if (stroke.cap == cap) {
    return;
  }
  stroke.cap = cap;
  invalidateContent();
}

void ShapeInstancedLayer::setLineJoin(LineJoin join) {
  if (stroke.join == join) {
    return;
  }
  stroke.join = join;
  invalidateContent();
}

void ShapeInstancedLayer::setMiterLimit(float limit) {
  if (stroke.miterLimit == limit) {
    return;
  }
  stroke.miterLimit = limit;
  invalidateContent();
}

void ShapeInstancedLayer::setLineWidth(float width) {
  if (stroke.width == width) {
    return;
  }
  stroke.width = width;
  invalidateContent();
}

void ShapeInstancedLayer::setLineDashPattern(const std::vector<float>& pattern) {
  if (_lineDashPattern.size() == pattern.size() &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  _lineDashPattern = pattern;
  invalidateContent();
}

void ShapeInstancedLayer::setLineDashPhase(float phase) {
  if (_lineDashPhase == phase) {
    return;
  }
  _lineDashPhase = phase;
  invalidateContent();
}

void ShapeInstancedLayer::setLineDashAdaptive(bool adaptive) {
  if (shapeBitFields.lineDashAdaptive == adaptive) {
    return;
  }
  shapeBitFields.lineDashAdaptive = adaptive;
  invalidateContent();
}

void ShapeInstancedLayer::setStrokeAlign(StrokeAlign align) {
  auto alignment = static_cast<uint8_t>(align);
  if (shapeBitFields.strokeAlign == alignment) {
    return;
  }
  shapeBitFields.strokeAlign = alignment;
  invalidateContent();
}

void ShapeInstancedLayer::setStrokeOnTop(bool value) {
  if (shapeBitFields.strokeOnTop == value) {
    return;
  }
  shapeBitFields.strokeOnTop = value;
  invalidateContent();
}

void ShapeInstancedLayer::onUpdateContent(LayerRecorder* recorder) {
  if (_shape == nullptr || _matrices.empty()) {
    return;
  }

  bool hasFill = !_fillStyles.empty() || !_colors.empty();
  bool hasStroke = stroke.width > 0 && (!_strokeStyles.empty() || !_colors.empty());

  if (!hasFill && !hasStroke) {
    return;
  }

  if (hasFill) {
    if (_fillStyles.empty()) {
      recorder->addShapeInstanced(_shape, _matrices, _colors, LayerPaint(Color::White()));
    } else {
      for (const auto& style : _fillStyles) {
        LayerPaint paint(style->color(), style->blendMode());
        paint.shader = style->shader();
        recorder->addShapeInstanced(_shape, _matrices, _colors, paint);
      }
    }
  } else {
    // Create a contour-only content for the shape (transparent color, no shader).
    recorder->addShapeInstanced(_shape, _matrices, _colors, LayerPaint(Color::Transparent()));
  }

  if (hasStroke) {
    auto strokeAlign = static_cast<StrokeAlign>(shapeBitFields.strokeAlign);
    bool simpleStroke = _lineDashPattern.empty() && strokeAlign == StrokeAlign::Center;
    std::shared_ptr<Shape> strokeShape = nullptr;
    if (!simpleStroke) {
      strokeShape = CreateStrokeShape(_shape, stroke, strokeAlign, _lineDashPattern, _lineDashPhase,
                                      shapeBitFields.lineDashAdaptive);
    }
    if (_strokeStyles.empty()) {
      LayerPaint paint(Color::White());
      if (shapeBitFields.strokeOnTop) {
        paint.placement = LayerPlacement::Foreground;
      }
      if (simpleStroke) {
        paint.style = PaintStyle::Stroke;
        paint.stroke = stroke;
        recorder->addShapeInstanced(_shape, _matrices, _colors, paint);
      } else {
        recorder->addShapeInstanced(strokeShape, _matrices, _colors, paint);
      }
    } else {
      for (const auto& style : _strokeStyles) {
        LayerPaint paint(style->color(), style->blendMode());
        paint.shader = style->shader();
        if (shapeBitFields.strokeOnTop) {
          paint.placement = LayerPlacement::Foreground;
        }
        if (simpleStroke) {
          paint.style = PaintStyle::Stroke;
          paint.stroke = stroke;
          recorder->addShapeInstanced(_shape, _matrices, _colors, paint);
        } else {
          recorder->addShapeInstanced(strokeShape, _matrices, _colors, paint);
        }
      }
    }
  }
}

}  // namespace tgfx
