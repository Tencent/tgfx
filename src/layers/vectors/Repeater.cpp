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

#include "tgfx/layers/vectors/Repeater.h"
#include <cmath>
#include <unordered_map>
#include "VectorContext.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<Repeater> Repeater::Make() {
  return std::shared_ptr<Repeater>(new Repeater());
}

void Repeater::setCopies(float value) {
  if (_copies == value) {
    return;
  }
  _copies = value;
  invalidateContent();
}

void Repeater::setOffset(float value) {
  if (_offset == value) {
    return;
  }
  _offset = value;
  invalidateContent();
}

void Repeater::setOrder(RepeaterOrder value) {
  if (_order == value) {
    return;
  }
  _order = value;
  invalidateContent();
}

void Repeater::setAnchor(const Point& value) {
  if (_anchor == value) {
    return;
  }
  _anchor = value;
  invalidateContent();
}

void Repeater::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  invalidateContent();
}

void Repeater::setRotation(float value) {
  if (_rotation == value) {
    return;
  }
  _rotation = value;
  invalidateContent();
}

void Repeater::setScale(const Point& value) {
  if (_scale == value) {
    return;
  }
  _scale = value;
  invalidateContent();
}

void Repeater::setStartAlpha(float value) {
  if (_startAlpha == value) {
    return;
  }
  _startAlpha = value;
  invalidateContent();
}

void Repeater::setEndAlpha(float value) {
  if (_endAlpha == value) {
    return;
  }
  _endAlpha = value;
  invalidateContent();
}

Matrix Repeater::getMatrix(float progress) const {
  auto matrix = Matrix::I();
  matrix.postTranslate(-_anchor.x, -_anchor.y);
  matrix.postScale(powf(_scale.x, progress), powf(_scale.y, progress));
  matrix.postRotate(_rotation * progress);
  matrix.postTranslate(_position.x * progress, _position.y * progress);
  matrix.postTranslate(_anchor.x, _anchor.y);
  return matrix;
}

static float Interpolate(float start, float end, float t) {
  return start + (end - start) * t;
}

void Repeater::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (context->geometries.empty() && context->painters.empty()) {
    return;
  }
  if (_copies < 0.0f) {
    return;
  }
  if (_copies == 0.0f) {
    context->geometries.clear();
    context->painters.clear();
    return;
  }

  auto maxCount = static_cast<int>(ceilf(_copies));
  std::vector<std::unique_ptr<Geometry>> originalGeometries = std::move(context->geometries);
  std::vector<std::unique_ptr<Painter>> originalPainters = std::move(context->painters);
  context->geometries.clear();
  context->painters.clear();
  context->geometries.reserve(originalGeometries.size() * static_cast<size_t>(maxCount));
  context->painters.reserve(originalPainters.size() * static_cast<size_t>(maxCount));

  auto startIndex = _order == RepeaterOrder::BelowOriginal ? maxCount - 1 : 0;
  auto endIndex = _order == RepeaterOrder::BelowOriginal ? -1 : maxCount;
  auto step = _order == RepeaterOrder::BelowOriginal ? -1 : 1;

  for (int i = startIndex; i != endIndex; i += step) {
    auto progress = static_cast<float>(i) + _offset;
    auto repeatMatrix = getMatrix(progress);
    auto alphaT = progress / static_cast<float>(maxCount);
    auto copyAlpha = Interpolate(_startAlpha, _endAlpha, alphaT);
    if (i == maxCount - 1) {
      copyAlpha *= _copies - static_cast<float>(i);
    }

    std::unordered_map<Geometry*, Geometry*> geometryMap = {};
    for (auto& geometry : originalGeometries) {
      auto cloned = geometry->clone();
      cloned->matrix.postConcat(repeatMatrix);
      geometryMap[geometry.get()] = cloned.get();
      context->geometries.push_back(std::move(cloned));
    }

    for (const auto& painter : originalPainters) {
      auto copyPainter = painter->clone();
      for (auto& geom : copyPainter->geometries) {
        auto it = geometryMap.find(geom);
        if (it != geometryMap.end()) {
          geom = it->second;
        }
      }
      copyPainter->applyAlpha(copyAlpha);
      context->painters.push_back(std::move(copyPainter));
    }
  }
}

}  // namespace tgfx
