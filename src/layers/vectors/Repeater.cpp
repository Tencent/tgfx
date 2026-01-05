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

#include "tgfx/layers/vectors/Repeater.h"
#include <cmath>
#include "VectorContext.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

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

void Repeater::setAnchorPoint(const Point& value) {
  if (_anchorPoint == value) {
    return;
  }
  _anchorPoint = value;
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
  matrix.postTranslate(-_anchorPoint.x, -_anchorPoint.y);
  matrix.postScale(powf(_scale.x, progress), powf(_scale.y, progress));
  matrix.postRotate(_rotation * progress);
  matrix.postTranslate(_position.x * progress, _position.y * progress);
  matrix.postTranslate(_anchorPoint.x, _anchorPoint.y);
  return matrix;
}

static float Interpolate(float start, float end, float t) {
  return start + (end - start) * t;
}

void Repeater::apply(VectorContext* context) {
  if (context->shapes.empty() && context->painters.empty()) {
    return;
  }
  if (_copies < 0.0f) {
    return;
  }
  if (_copies == 0.0f) {
    context->shapes.clear();
    context->matrices.clear();
    context->painters.clear();
    return;
  }

  auto maxCount = static_cast<int>(ceilf(_copies));
  std::vector<std::shared_ptr<Shape>> originalShapes = std::move(context->shapes);
  std::vector<Matrix> originalMatrices = std::move(context->matrices);
  std::vector<std::unique_ptr<Painter>> originalPainters = std::move(context->painters);
  context->shapes.clear();
  context->matrices.clear();
  context->painters.clear();

  for (int i = 0; i < maxCount; i++) {
    auto progress = static_cast<float>(i) + _offset;
    auto repeatMatrix = getMatrix(progress);
    auto alphaT = progress / static_cast<float>(maxCount);
    auto copyAlpha = Interpolate(_startAlpha, _endAlpha, alphaT);
    if (i == maxCount - 1) {
      copyAlpha *= _copies - static_cast<float>(i);
    }

    std::vector<std::shared_ptr<Shape>> copyShapes = {};
    std::vector<Matrix> copyMatrices = {};
    for (size_t j = 0; j < originalShapes.size(); j++) {
      copyShapes.push_back(originalShapes[j]);
      auto matrix = originalMatrices[j];
      matrix.postConcat(repeatMatrix);
      copyMatrices.push_back(matrix);
    }
    std::vector<std::unique_ptr<Painter>> copyPainters = {};
    for (const auto& painter : originalPainters) {
      auto copyPainter = painter->clone();
      copyPainter->applyTransform(repeatMatrix, copyAlpha);
      copyPainters.push_back(std::move(copyPainter));
    }

    if (_order == RepeaterOrder::BelowOriginal) {
      // BelowOriginal: copies are below original, so original (last) is on top
      // Insert new copies at the beginning so they are drawn first (below)
      for (auto& painter : context->painters) {
        painter->offsetShapeIndex(copyShapes.size());
      }
      context->shapes.insert(context->shapes.begin(), std::make_move_iterator(copyShapes.begin()),
                             std::make_move_iterator(copyShapes.end()));
      context->matrices.insert(context->matrices.begin(),
                               std::make_move_iterator(copyMatrices.begin()),
                               std::make_move_iterator(copyMatrices.end()));
      context->painters.insert(context->painters.begin(),
                               std::make_move_iterator(copyPainters.begin()),
                               std::make_move_iterator(copyPainters.end()));
    } else {
      // AboveOriginal: copies are above original, so original (first) is below
      // Insert new copies at the end so they are drawn last (on top)
      auto painterStartIndex = context->shapes.size();
      for (auto& painter : copyPainters) {
        painter->offsetShapeIndex(painterStartIndex);
      }
      context->shapes.insert(context->shapes.end(), std::make_move_iterator(copyShapes.begin()),
                             std::make_move_iterator(copyShapes.end()));
      context->matrices.insert(context->matrices.end(),
                               std::make_move_iterator(copyMatrices.begin()),
                               std::make_move_iterator(copyMatrices.end()));
      context->painters.insert(context->painters.end(),
                               std::make_move_iterator(copyPainters.begin()),
                               std::make_move_iterator(copyPainters.end()));
    }
  }
}

}  // namespace tgfx
