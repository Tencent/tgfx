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
#include "tgfx/core/PathMeasure.h"

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
  for (const auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  _fillStyles = std::move(fills);
  for (const auto& style : _fillStyles) {
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
  if (_strokeAlign == align) {
    return;
  }
  _strokeAlign = align;
  invalidateContent();
}

ShapeLayer::~ShapeLayer() {
  for (auto& style : _fillStyles) {
    detachProperty(style.get());
  }
  for (auto& style : _strokeStyles) {
    detachProperty(style.get());
  }
}

std::unique_ptr<LayerContent> ShapeLayer::onUpdateContent() {
  if (_shape == nullptr) {
    return nullptr;
  }
  std::vector<std::unique_ptr<LayerContent>> contents = {};
  contents.reserve(_fillStyles.size() + _strokeStyles.size());
  for (auto& style : _fillStyles) {
    contents.push_back(std::make_unique<ShapeContent>(_shape, style->getShader(), style->alpha(),
                                                      style->blendMode()));
  }
  if (stroke.width > 0 && !_strokeStyles.empty()) {
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
    for (auto& style : _strokeStyles) {
      auto content = std::make_unique<ShapeContent>(strokeShape, style->getShader(), style->alpha(),
                                                    style->blendMode());
      contents.push_back(std::move(content));
    }
  }
  return LayerContent::Compose(std::move(contents));
}
}  // namespace tgfx
