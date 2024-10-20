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
#include "layers/contents/ShapeContent.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PathMeasure.h"

namespace tgfx {
std::shared_ptr<ShapeLayer> ShapeLayer::Make() {
  auto layer = std::shared_ptr<ShapeLayer>(new ShapeLayer());
  layer->weakThis = layer;
  return layer;
}

void ShapeLayer::setPath(Path path) {
  if (_path == path) {
    return;
  }
  _path = std::move(path);
  _pathProvider = nullptr;
  invalidateContent();
}

void ShapeLayer::setPathProvider(std::shared_ptr<PathProvider> value) {
  if (_pathProvider == value) {
    return;
  }
  _pathProvider = std::move(value);
  _path.reset();
  invalidateContent();
}

void ShapeLayer::setFillStyle(std::shared_ptr<ShapeStyle> style) {
  if (_fillStyle == style) {
    return;
  }
  _fillStyle = std::move(style);
  invalidateContent();
}

void ShapeLayer::setStrokeStyle(std::shared_ptr<ShapeStyle> style) {
  if (_strokeStyle == style) {
    return;
  }
  _strokeStyle = std::move(style);
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

std::unique_ptr<LayerContent> ShapeLayer::onUpdateContent() {
  std::vector<std::unique_ptr<LayerContent>> contents = {};
  auto path = _path.isEmpty() ? _pathProvider->getPath() : _path;
  if (_fillStyle) {
    auto content = std::make_unique<ShapeContent>(path, _fillStyle->getShader());
    contents.push_back(std::move(content));
  }
  if (stroke.width > 0 && _strokeStyle) {
    auto strokedPath = path;
    if ((_strokeStart != 0 || _strokeEnd != 1)) {
      auto pathMeasure = PathMeasure::MakeFrom(path);
      auto length = pathMeasure->getLength();
      auto start = _strokeStart * length;
      auto end = _strokeEnd * length;
      Path tempPath = {};
      if (pathMeasure->getSegment(start, end, &tempPath)) {
        strokedPath = tempPath;
      }
    }
    if (!_lineDashPattern.empty()) {
      auto dashes = _lineDashPattern;
      if (_lineDashPattern.size() % 2 != 0) {
        dashes.insert(dashes.end(), _lineDashPattern.begin(), _lineDashPattern.end());
      }
      auto pathEffect =
          PathEffect::MakeDash(dashes.data(), static_cast<int>(dashes.size()), _lineDashPhase);
      pathEffect->applyTo(&strokedPath);
    }
    auto strokeEffect = PathEffect::MakeStroke(&stroke);
    strokeEffect->applyTo(&strokedPath);
    auto content = std::make_unique<ShapeContent>(strokedPath, _strokeStyle->getShader());
    contents.push_back(std::move(content));
  }
  return LayerContent::Compose(std::move(contents));
}
}  // namespace tgfx
