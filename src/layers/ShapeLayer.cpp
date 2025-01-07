/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/Profiling.h"
#include "layers/contents/ShapeContent.h"
#include "tgfx/core/PathEffect.h"

namespace tgfx {
std::shared_ptr<ShapeLayer> ShapeLayer::Make() {
  TRACE_EVENT;
  auto layer = std::shared_ptr<ShapeLayer>(new ShapeLayer());
  layer->weakThis = layer;
  return layer;
}

Path ShapeLayer::path() const {
  if (_shape == nullptr) {
    return {};
  }
  Path path = {};
  if (_shape->isSimplePath(&path)) {
    return path;
  }
  return {};
}

void ShapeLayer::setPath(Path path) {
  Path oldPath = {};
  if (_shape && _shape->isSimplePath(&oldPath) && oldPath == path) {
    return;
  }
  _shape = Shape::MakeFrom(std::move(path));
  invalidateContentAndContour();
}

void ShapeLayer::setShape(std::shared_ptr<Shape> value) {
  if (_shape == value) {
    return;
  }
  _shape = std::move(value);
  invalidateContentAndContour();
}

void ShapeLayer::setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills) {
  if (_fillStyles.size() == fills.size() &&
      std::equal(_fillStyles.begin(), _fillStyles.end(), fills.begin())) {
    return;
  }
  for (const auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  _fillStyles = std::move(fills);
  for (const auto& style : _fillStyles) {
    attachProperty(style.get());
  }
  invalidateContentAndContour();
}

void ShapeLayer::removeFillStyles() {
  if (_fillStyles.empty()) {
    return;
  }
  for (const auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  _fillStyles = {};
  invalidateContentAndContour();
}

void ShapeLayer::setFillStyle(std::shared_ptr<ShapeStyle> fill) {
  if (fill == nullptr) {
    removeFillStyles();
  } else {
    setFillStyles({std::move(fill)});
  }
}

void ShapeLayer::addFillStyle(std::shared_ptr<ShapeStyle> fillStyle) {
  if (fillStyle == nullptr) {
    return;
  }
  attachProperty(fillStyle.get());
  _fillStyles.push_back(std::move(fillStyle));
  invalidateContentAndContour();
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
  invalidateContentAndContour();
}

void ShapeLayer::removeStrokeStyles() {
  if (_strokeStyles.empty()) {
    return;
  }
  for (const auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
  _strokeStyles = {};
  invalidateContentAndContour();
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
  invalidateContentAndContour();
}

void ShapeLayer::setLineCap(LineCap cap) {
  if (stroke.cap == cap) {
    return;
  }
  stroke.cap = cap;
  invalidateContentAndContour();
}

void ShapeLayer::setLineJoin(LineJoin join) {
  if (stroke.join == join) {
    return;
  }
  stroke.join = join;
  invalidateContentAndContour();
}

void ShapeLayer::setMiterLimit(float limit) {
  if (stroke.miterLimit == limit) {
    return;
  }
  stroke.miterLimit = limit;
  invalidateContentAndContour();
}

void ShapeLayer::setLineWidth(float width) {
  if (stroke.width == width) {
    return;
  }
  stroke.width = width;
  invalidateContentAndContour();
}

void ShapeLayer::setLineDashPattern(const std::vector<float>& pattern) {
  if (_lineDashPattern.size() == pattern.size() &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  _lineDashPattern = pattern;
  invalidateContentAndContour();
}

void ShapeLayer::setLineDashPhase(float phase) {
  if (_lineDashPhase == phase) {
    return;
  }
  _lineDashPhase = phase;
  invalidateContentAndContour();
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
  invalidateContentAndContour();
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
  invalidateContentAndContour();
}

void ShapeLayer::setStrokeAlign(StrokeAlign align) {
  if (_strokeAlign == align) {
    return;
  }
  _strokeAlign = align;
  invalidateContentAndContour();
}

ShapeLayer::~ShapeLayer() {
  for (auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  for (auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
}

std::shared_ptr<Shape> ShapeLayer::createStrokeShape() const {
  if (stroke.width <= 0 || _shape == nullptr) {
    return nullptr;
  }
  auto strokeShape = _shape;
  if ((_strokeStart != 0 || _strokeEnd != 1)) {
    auto pathEffect = PathEffect::MakeTrim(_strokeStart, _strokeEnd);
    strokeShape = Shape::ApplyEffect(std::move(strokeShape), std::move(pathEffect));
  }
  if (!_lineDashPattern.empty()) {
    auto dashes = _lineDashPattern;
    if (_lineDashPattern.size() % 2 != 0) {
      dashes.insert(dashes.end(), _lineDashPattern.begin(), _lineDashPattern.end());
    }
    auto pathEffect =
        PathEffect::MakeDash(dashes.data(), static_cast<int>(dashes.size()), _lineDashPhase);
    strokeShape = Shape::ApplyEffect(std::move(strokeShape), std::move(pathEffect));
  }
  if (_strokeAlign != StrokeAlign::Center) {
    auto tempStroke = stroke;
    tempStroke.width *= 2;
    strokeShape = Shape::ApplyStroke(std::move(strokeShape), &tempStroke);
    if (_strokeAlign == StrokeAlign::Inside) {
      strokeShape = Shape::Merge(std::move(strokeShape), _shape, PathOp::Intersect);
    } else {
      strokeShape = Shape::Merge(std::move(strokeShape), _shape, PathOp::Difference);
    }
  } else {
    strokeShape = Shape::ApplyStroke(std::move(strokeShape), &stroke);
  }
  return strokeShape;
}

std::unique_ptr<LayerContent> ShapeLayer::onUpdateContent() {
  if (_shape == nullptr) {
    return nullptr;
  }
  std::vector<std::unique_ptr<LayerContent>> contents = {};
  contents.reserve(_fillStyles.size() + _strokeStyles.size());
  for (auto& style : _fillStyles) {
    if (style->alpha() <= 0) {
      continue;
    }
    contents.push_back(std::make_unique<ShapeContent>(_shape, style->getShader(), style->alpha(),
                                                      style->blendMode()));
  }
  if (!_strokeStyles.empty()) {
    if (auto strokeShape = createStrokeShape()) {
      for (auto& style : _strokeStyles) {
        if (style->alpha() <= 0) {
          continue;
        }
        auto content = std::make_unique<ShapeContent>(strokeShape, style->getShader(),
                                                      style->alpha(), style->blendMode());
        contents.push_back(std::move(content));
      }
    }
  }
  return LayerContent::Compose(std::move(contents));
}

LayerContent* ShapeLayer::getContour() {
  if (contourContent) {
    return contourContent.get();
  }
  if (_shape == nullptr) {
    return nullptr;
  }

  contourContent = CreateContourWithStyles(_shape, _fillStyles);
  if (contourContent == nullptr) {
    contourContent =
        std::make_unique<ShapeContent>(_shape, Shader::MakeColorShader(Color::White()));
  }

  if (!_strokeStyles.empty()) {
    auto strokeShape = createStrokeShape();
    if (auto strokeContour = CreateContourWithStyles(strokeShape, _strokeStyles)) {
      std::vector<std::unique_ptr<LayerContent>> contours(2);
      contours[0] = std::move(contourContent);
      contours[1] = std::move(strokeContour);
      contourContent = LayerContent::Compose(std::move(contours));
    }
  }
  return contourContent.get();
}

std::unique_ptr<LayerContent> ShapeLayer::CreateContourWithStyles(
    std::shared_ptr<Shape> shape, const std::vector<std::shared_ptr<ShapeStyle>>& styles) {
  if (shape == nullptr || styles.empty()) {
    return nullptr;
  }
  std::vector<std::unique_ptr<LayerContent>> contours = {};
  auto isAllImageStyle = std::none_of(styles.begin(), styles.end(), [](const auto& style) {
    return style->alpha() > 0 && !style->isImage();
  });
  if (!isAllImageStyle) {
    contours.push_back(
        std::make_unique<ShapeContent>(shape, Shader::MakeColorShader(Color::White())));
  } else {
    for (const auto& style : styles) {
      if (style->alpha() > 0) {
        contours.push_back(std::make_unique<ShapeContent>(shape, style->getShader()));
      }
    }
  }
  return LayerContent::Compose(std::move(contours));
}

void ShapeLayer::invalidateContentAndContour() {
  invalidateContent();
  contourContent = nullptr;
}

}  // namespace tgfx
