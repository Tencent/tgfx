/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/ShapeLayer.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/layers/SolidColor.h"

namespace tgfx {
std::shared_ptr<ShapeLayer> ShapeLayer::Make() {
  return std::shared_ptr<ShapeLayer>(new ShapeLayer());
}

Path ShapeLayer::path() const {
  if (_shape == nullptr) {
    return {};
  }
  return _shape->isSimplePath() ? _shape->getPath() : Path();
}

void ShapeLayer::setPath(Path path) {
  Path oldPath = {};
  if (_shape && _shape->isSimplePath() && _shape->getPath() == path) {
    return;
  }
  _shape = Shape::MakeFrom(std::move(path));
  invalidateContent();
}

void ShapeLayer::setShape(std::shared_ptr<Shape> value) {
  if (_shape == value) {
    return;
  }
  _shape = std::move(value);
  invalidateContent();
}

void ShapeLayer::setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills) {
  if (_fillStyles.size() == fills.size() &&
      std::equal(_fillStyles.begin(), _fillStyles.end(), fills.begin())) {
    return;
  }
  for (auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  _fillStyles = std::move(fills);
  for (auto& style : _fillStyles) {
    attachProperty(style.get());
  }
  invalidateContent();
}

void ShapeLayer::removeFillStyles() {
  if (_fillStyles.empty()) {
    return;
  }
  for (const auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  _fillStyles = {};
  invalidateContent();
}

void ShapeLayer::setFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    removeFillStyles();
  } else {
    setFillStyles({std::move(fillStyle)});
  }
}

void ShapeLayer::addFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    return;
  }
  attachProperty(fillStyle.get());
  _fillStyles.push_back(std::move(fillStyle));
  invalidateContent();
}

void ShapeLayer::setStrokeStyles(std::vector<std::shared_ptr<ShapeStyle>> strokes) {
  if (_strokeStyles.size() == strokes.size() &&
      std::equal(_strokeStyles.begin(), _strokeStyles.end(), strokes.begin())) {
    return;
  }
  for (const auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
  _strokeStyles = std::move(strokes);
  for (const auto& style : _strokeStyles) {
    attachProperty(style.get());
  }
  invalidateContent();
}

void ShapeLayer::removeStrokeStyles() {
  if (_strokeStyles.empty()) {
    return;
  }
  for (const auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
  _strokeStyles = {};
  invalidateContent();
}

void ShapeLayer::setStrokeStyle(std::shared_ptr<ShapeStyle> stroke) {
  if (stroke == nullptr) {
    removeStrokeStyles();
  } else {
    setStrokeStyles({std::move(stroke)});
  }
}

void ShapeLayer::addStrokeStyle(std::shared_ptr<ShapeStyle> strokeStyle) {
  if (strokeStyle == nullptr) {
    return;
  }
  attachProperty(strokeStyle.get());
  _strokeStyles.push_back(std::move(strokeStyle));
  invalidateContent();
}

void ShapeLayer::setLineCap(LineCap cap) {
  if (stroke.cap == cap) {
    return;
  }
  stroke.cap = cap;
  invalidateContent();
}

void ShapeLayer::setLineJoin(LineJoin join) {
  if (stroke.join == join) {
    return;
  }
  stroke.join = join;
  invalidateContent();
}

void ShapeLayer::setMiterLimit(float limit) {
  if (stroke.miterLimit == limit) {
    return;
  }
  stroke.miterLimit = limit;
  invalidateContent();
}

void ShapeLayer::setLineWidth(float width) {
  if (stroke.width == width) {
    return;
  }
  stroke.width = width;
  invalidateContent();
}

void ShapeLayer::setLineDashPattern(const std::vector<float>& pattern) {
  if (_lineDashPattern.size() == pattern.size() &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  _lineDashPattern = pattern;
  invalidateContent();
}

void ShapeLayer::setLineDashPhase(float phase) {
  if (_lineDashPhase == phase) {
    return;
  }
  _lineDashPhase = phase;
  invalidateContent();
}

void ShapeLayer::setLineDashAdaptive(bool adaptive) {
  if (shapeBitFields.lineDashAdaptive == adaptive) {
    return;
  }
  shapeBitFields.lineDashAdaptive = adaptive;
  invalidateContent();
}

void ShapeLayer::setStrokeStart(float start) {
  if (start < 0) {
    start = 0;
  }
  if (start > 1.0f) {
    start = 1.0f;
  }
  if (_strokeStart == start) {
    return;
  }
  _strokeStart = start;
  invalidateContent();
}

void ShapeLayer::setStrokeEnd(float end) {
  if (end < 0) {
    end = 0;
  }
  if (end > 1.0f) {
    end = 1.0f;
  }
  if (_strokeEnd == end) {
    return;
  }
  _strokeEnd = end;
  invalidateContent();
}

void ShapeLayer::setStrokeAlign(StrokeAlign align) {
  auto alignment = static_cast<uint8_t>(align);
  if (shapeBitFields.strokeAlign == alignment) {
    return;
  }
  shapeBitFields.strokeAlign = alignment;
  invalidateContent();
}

void ShapeLayer::setStrokeOnTop(bool value) {
  if (shapeBitFields.strokeOnTop == value) {
    return;
  }
  shapeBitFields.strokeOnTop = value;
  invalidateContent();
}

ShapeLayer::ShapeLayer() {
  memset(&shapeBitFields, 0, sizeof(shapeBitFields));
}

ShapeLayer::~ShapeLayer() {
  for (auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  for (auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
}

static bool DrawContour(Canvas* canvas, std::shared_ptr<Shape> shape,
                        const std::vector<Paint>& paints) {
  if (shape == nullptr || paints.empty()) {
    return false;
  }
  auto hasNonImageShader = std::any_of(paints.begin(), paints.end(), [](const Paint& paint) {
    auto shader = paint.getShader();
    return !shader || !shader->isAImage();
  });
  if (hasNonImageShader) {
    canvas->drawShape(shape, {});
  } else {
    for (auto& paint : paints) {
      canvas->drawShape(shape, paint);
    }
  }
  return true;
}

void ShapeLayer::onUpdateContent(LayerRecorder* recorder) {
  if (_shape == nullptr) {
    return;
  }
  auto fillPaints = createShapePaints(_fillStyles);
  auto strokePaints = createShapePaints(_strokeStyles);
  auto strokeShape = strokePaints.empty() ? nullptr : createStrokeShape();
  auto canvas = recorder->getCanvas(LayerContentType::Default);
  for (auto& paint : fillPaints) {
    canvas->drawShape(_shape, paint);
  }
  if (shapeBitFields.strokeOnTop) {
    canvas = recorder->getCanvas(LayerContentType::Foreground);
  }
  for (auto& paint : strokePaints) {
    canvas->drawShape(strokeShape, paint);
  }
  canvas = recorder->getCanvas(LayerContentType::Contour);
  if (!DrawContour(canvas, _shape, fillPaints)) {
    // If there is not any fill paints, we still need to draw the shape as contour.
    canvas->drawShape(_shape, {});
  }
  DrawContour(canvas, strokeShape, strokePaints);
}

std::vector<Paint> ShapeLayer::createShapePaints(
    const std::vector<std::shared_ptr<ShapeStyle>>& styles) const {
  std::vector<Paint> paintList = {};
  paintList.reserve(styles.size());
  for (auto& style : styles) {
    Paint paint = {};
    paint.setAlpha(style->alpha());
    paint.setBlendMode(style->blendMode());
    paint.setShader(style->getShader());
    paintList.push_back(paint);
  }
  return paintList;
}

std::shared_ptr<Shape> ShapeLayer::createStrokeShape() const {
  auto strokeShape = _shape;
  if ((_strokeStart != 0 || _strokeEnd != 1)) {
    auto pathEffect = PathEffect::MakeTrim(_strokeStart, _strokeEnd);
    strokeShape = Shape::ApplyEffect(std::move(strokeShape), std::move(pathEffect));
  }
  auto strokeAlign = static_cast<StrokeAlign>(shapeBitFields.strokeAlign);
  auto tempStroke = stroke;
  if (strokeAlign != StrokeAlign::Center) {
    tempStroke.width *= 2;
  }
  if (!_lineDashPattern.empty()) {
    auto dashes = _lineDashPattern;
    if (_lineDashPattern.size() % 2 != 0) {
      dashes.insert(dashes.end(), _lineDashPattern.begin(), _lineDashPattern.end());
    }
    dashes = SimplifyLineDashPattern(dashes, tempStroke);
    // dashes may be simplified to solid line.
    if (!dashes.empty()) {
      auto dash = PathEffect::MakeDash(dashes.data(), static_cast<int>(dashes.size()),
                                       _lineDashPhase, shapeBitFields.lineDashAdaptive);
      strokeShape = Shape::ApplyEffect(std::move(strokeShape), std::move(dash));
    }
  }
  strokeShape = Shape::ApplyStroke(std::move(strokeShape), &tempStroke);
  if (strokeAlign == StrokeAlign::Inside) {
    strokeShape = Shape::Merge(std::move(strokeShape), _shape, PathOp::Intersect);
  } else if (strokeAlign == StrokeAlign::Outside) {
    strokeShape = Shape::Merge(std::move(strokeShape), _shape, PathOp::Difference);
  }
  return strokeShape;
}

std::optional<Path> ShapeLayer::getClipPath(bool contour) const {
  // Only return the path if there's no stroke (we only use fill shape for clipping)
  // and the shape is a simple path (otherwise getPath() is expensive).
  if (_shape == nullptr || !_shape->isSimplePath() || !_strokeStyles.empty()) {
    return std::nullopt;
  }
  // For Contour mask type, skip alpha checks but still reject ImagePattern
  // (ImagePattern may have transparent regions that affect the contour).
  if (contour) {
    for (const auto& style : _fillStyles) {
      if (style->getType() == ShapeStyle::Type::ImagePattern) {
        return std::nullopt;
      }
    }
    return _shape->getPath();
  }
  // For Alpha mask type, we need fully opaque fill content to use clip path optimization.
  // Check if all fill styles are fully opaque SolidColors.
  for (const auto& style : _fillStyles) {
    if (style->alpha() < 1.0f) {
      return std::nullopt;
    }
    // For Gradient and ImagePattern, we can't guarantee opacity, so don't optimize
    if (style->getType() != ShapeStyle::Type::SolidColor) {
      return std::nullopt;
    }
    // Check if the style is a SolidColor with opaque color
    auto solidColor = static_cast<SolidColor*>(style.get());
    if (solidColor->color().alpha < 1.0f) {
      return std::nullopt;
    }
  }
  return _shape->getPath();
}
}  // namespace tgfx
