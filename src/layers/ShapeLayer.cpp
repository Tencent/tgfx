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

void ShapeLayer::setPath(Path path) {
  if (_path == path) {
    return;
  }
  _path = std::move(path);
  _pathProvider = nullptr;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setPathProvider(std::shared_ptr<PathProvider> value) {
  if (_pathProvider == value) {
    return;
  }
  _pathProvider = std::move(value);
  _path.reset();
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setFillStyle(std::shared_ptr<ShapeStyle> style) {
  if (_fillStyle == style) {
    return;
  }
  _fillStyle = std::move(style);
  invalidate();
}

void ShapeLayer::setStrokeStyle(std::shared_ptr<ShapeStyle> style) {
  if (_strokeStyle == style) {
    return;
  }
  _strokeStyle = std::move(style);
  invalidate();
}

void ShapeLayer::setLineCap(LineCap cap) {
  if (stroke.cap == cap) {
    return;
  }
  stroke.cap = cap;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setLineJoin(LineJoin join) {
  if (stroke.join == join) {
    return;
  }
  stroke.join = join;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setMiterLimit(float limit) {
  if (stroke.miterLimit == limit) {
    return;
  }
  stroke.miterLimit = limit;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setLineWidth(float width) {
  if (stroke.width == width) {
    return;
  }
  stroke.width = width;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setLineDashPattern(const std::vector<float>& pattern) {
  if (_lineDashPattern.size() == pattern.size() &&
      std::equal(_lineDashPattern.begin(), _lineDashPattern.end(), pattern.begin())) {
    return;
  }
  _lineDashPattern = pattern;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setLineDashPhase(float phase) {
  if (_lineDashPhase == phase) {
    return;
  }
  _lineDashPhase = phase;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setStrokeStart(float start) {
  if (_strokeStart == start) {
    return;
  }
  _strokeStart = start;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::setStrokeEnd(float end) {
  if (_strokeEnd == end) {
    return;
  }
  _strokeEnd = end;
  invalidate();
  invalidateRenderPath = true;
}

void ShapeLayer::updateRenderPath() {
  if (invalidateRenderPath) {
    renderPath = _path.isEmpty() ? _pathProvider->getPath() : _path;
    if (!_lineDashPattern.empty()) {
      auto renderLineDashPattern = _lineDashPattern;
      if (_lineDashPattern.size() % 2 != 0) {
        renderLineDashPattern.insert(renderLineDashPattern.end(), _lineDashPattern.begin(), _lineDashPattern.end());
      }
      auto pathEffect = PathEffect::MakeDash(
          renderLineDashPattern.data(), static_cast<int>(renderLineDashPattern.size()), _lineDashPhase);
      pathEffect->applyTo(&renderPath);
    }
    auto strokeEffect = PathEffect::MakeStroke(&stroke);
    strokeEffect->applyTo(&renderPath);

    invalidateRenderPath = false;
  }
}

void ShapeLayer::onDraw(Canvas* canvas, float alpha) {
  if (invalidateRenderPath) {
    updateRenderPath();
  }
  if (renderPath.isEmpty()) {
    return ;
  }
  Paint shapePaint = {};
  if (_fillStyle) {
    shapePaint.setShader(_fillStyle->getShader());
  }
  shapePaint.setAlpha(alpha);
  auto shapePath = _path.isEmpty() ? _pathProvider->getPath() : _path;
  canvas->drawPath(shapePath, shapePaint);

  if (stroke.width > 0) {
    Paint strokePaint = {};
    strokePaint.setAlpha(alpha);
    if (_strokeStyle) {
      strokePaint.setShader(_strokeStyle->getShader());
    }
    canvas->drawPath(renderPath, strokePaint);
  }
}

void ShapeLayer::measureContentBounds(Rect* bounds) {
  if (invalidateRenderPath) {
    updateRenderPath();
  }
  if (renderPath.isEmpty()) {
    bounds->setEmpty();
  } else {
    *bounds = renderPath.getBounds();
  }
}
}  // namespace tgfx
