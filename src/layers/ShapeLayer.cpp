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
#include "tgfx/core/PathEffect.h"

namespace tgfx {
std::shared_ptr<ShapeLayer> ShapeLayer::Make() {
  auto layer = std::shared_ptr<ShapeLayer>(new ShapeLayer());
  layer->weakThis = layer;
  return layer;
}

void ShapeLayer::setPath(const Path& path) {
  if (_path == path) {
    return;
  }
  _path = path;
  _pathProvider = nullptr;
  invalidate();
}

void ShapeLayer::setPathProvider(const std::shared_ptr<PathProvider>& value) {
  if (_pathProvider == value) {
    return;
  }
  _pathProvider = value;
  _path.reset();
  invalidate();
}

void ShapeLayer::setFillStyle(const std::shared_ptr<ShapeStyle>& style) {
  if (_fillStyle == style) {
    return;
  }
  _fillStyle = style;
  invalidate();
}

void ShapeLayer::setStrokeStyle(const std::shared_ptr<ShapeStyle>& style) {
  if (_strokeStyle == style) {
    return;
  }
  _strokeStyle = style;
  invalidate();
}

void ShapeLayer::setLineCap(LineCap cap) {
  if (stroke.cap == cap) {
    return;
  }
  stroke.cap = cap;
  invalidate();
}

void ShapeLayer::setLineJoin(LineJoin join) {
  if (stroke.join == join) {
    return;
  }
  stroke.join = join;
  invalidate();
}

void ShapeLayer::setMiterLimit(float limit) {
  if (stroke.miterLimit == limit) {
    return;
  }
  stroke.miterLimit = limit;
  invalidate();
}

void ShapeLayer::setLineWidth(float width) {
  if (stroke.width == width) {
    return;
  }
  stroke.width = width;
  invalidate();
}

void ShapeLayer::setLineDashPattern(const std::vector<float>& pattern) {
  if (_lineDashPattern.size() == pattern.size() &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  if (_lineDashPattern.size() == pattern.size() * 2 &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin()) &&
      std::equal(_lineDashPattern.begin() + static_cast<int>(pattern.size()),
                 _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  _lineDashPattern = pattern;
  if (pattern.size() % 2 != 0) {
    _lineDashPattern.insert(_lineDashPattern.end(), pattern.begin(), pattern.end());
  }
  invalidate();
}

void ShapeLayer::setLineDashPhase(float phase) {
  if (_lineDashPhase == phase) {
    return;
  }
  _lineDashPhase = phase;
  invalidate();
}

void ShapeLayer::setStrokeStart(float start) {
  if (_strokeStart == start) {
    return;
  }
  _strokeStart = start;
  invalidate();
}

void ShapeLayer::setStrokeEnd(float end) {
  if (_strokeEnd == end) {
    return;
  }
  _strokeEnd = end;
  invalidate();
}

void ShapeLayer::onDraw(Canvas* canvas, float alpha) {
  if (_path.isEmpty() && _pathProvider == nullptr) {
    return;
  }
  Paint shapePaint = {};
  if (_fillStyle) {
    shapePaint.setShader(_fillStyle->getShader());
  }
  shapePaint.setAlpha(alpha);
  renderPath = _path.isEmpty() ? _pathProvider->getPath() : _path;
  canvas->drawPath(renderPath, shapePaint);

  if (stroke.width > 0) {
    if (!_lineDashPattern.empty()) {
      auto pathEffect = PathEffect::MakeDash(
          _lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()), _lineDashPhase);
      pathEffect->applyTo(&renderPath);
    }
    auto strokeEffect = PathEffect::MakeStroke(&stroke);
    strokeEffect->applyTo(&renderPath);

    Paint strokePaint = {};
    if (_strokeStyle) {
      strokePaint.setShader(_strokeStyle->getShader());
    }
    canvas->drawPath(renderPath, strokePaint);
  }
}

void ShapeLayer::measureContentBounds(tgfx::Rect* bounds) const {
  if (renderPath.isEmpty()) {
    bounds->setEmpty();
  } else {
    *bounds = renderPath.getBounds();
  }
}
}  // namespace tgfx
