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

#include "tgfx/layers/vectors/VectorGroup.h"
#include <cmath>
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

static float DegreesToRadians(float degrees) {
  return degrees * static_cast<float>(M_PI) / 180.0f;
}

static void ApplySkew(Matrix* matrix, float skew, float skewAxis) {
  if (skew == 0.0f && skewAxis == 0.0f) {
    return;
  }
  auto u = cosf(skewAxis);
  auto v = sinf(skewAxis);
  Matrix temp = {};
  temp.setAll(u, -v, 0, v, u, 0);
  matrix->postConcat(temp);
  auto w = tanf(skew);
  temp.setAll(1, w, 0, 0, 1, 0);
  matrix->postConcat(temp);
  temp.setAll(u, v, 0, -v, u, 0);
  matrix->postConcat(temp);
}

void VectorGroup::setElements(std::vector<std::shared_ptr<VectorElement>> value) {
  for (const auto& owner : owners) {
    for (const auto& element : _elements) {
      DEBUG_ASSERT(element != nullptr);
      element->detachFromLayer(owner);
    }
  }
  _elements.clear();
  for (auto& element : value) {
    if (element == nullptr) {
      continue;
    }
    for (const auto& owner : owners) {
      element->attachToLayer(owner);
    }
    _elements.push_back(std::move(element));
  }
  invalidateContent();
}

void VectorGroup::attachToLayer(Layer* layer) {
  LayerProperty::attachToLayer(layer);
  for (const auto& element : _elements) {
    DEBUG_ASSERT(element != nullptr);
    element->attachToLayer(layer);
  }
}

void VectorGroup::detachFromLayer(Layer* layer) {
  for (const auto& element : _elements) {
    DEBUG_ASSERT(element != nullptr);
    element->detachFromLayer(layer);
  }
  LayerProperty::detachFromLayer(layer);
}

void VectorGroup::setAnchorPoint(const Point& value) {
  if (_anchorPoint == value) {
    return;
  }
  _anchorPoint = value;
  _matrixDirty = true;
  invalidateContent();
}

void VectorGroup::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  _matrixDirty = true;
  invalidateContent();
}

void VectorGroup::setScale(const Point& value) {
  if (_scale == value) {
    return;
  }
  _scale = value;
  _matrixDirty = true;
  invalidateContent();
}

void VectorGroup::setRotation(float value) {
  if (_rotation == value) {
    return;
  }
  _rotation = value;
  _matrixDirty = true;
  invalidateContent();
}

void VectorGroup::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void VectorGroup::setSkew(float value) {
  if (_skew == value) {
    return;
  }
  _skew = value;
  _matrixDirty = true;
  invalidateContent();
}

void VectorGroup::setSkewAxis(float value) {
  if (_skewAxis == value) {
    return;
  }
  _skewAxis = value;
  _matrixDirty = true;
  invalidateContent();
}

Matrix VectorGroup::getMatrix() {
  if (_matrixDirty) {
    _cachedMatrix = Matrix::I();
    _cachedMatrix.postTranslate(-_anchorPoint.x, -_anchorPoint.y);
    _cachedMatrix.postScale(_scale.x, _scale.y);
    if (_skew != 0.0f) {
      ApplySkew(&_cachedMatrix, DegreesToRadians(-_skew), DegreesToRadians(_skewAxis));
    }
    _cachedMatrix.postRotate(_rotation);
    _cachedMatrix.postTranslate(_position.x, _position.y);
    _matrixDirty = false;
  }
  return _cachedMatrix;
}

void VectorGroup::apply(VectorContext* context) {
  auto groupMatrix = getMatrix();

  VectorContext groupContext = {};

  for (const auto& element : _elements) {
    if (element && element->enabled()) {
      element->apply(&groupContext);
    }
  }

  // Merge shapes and matrices
  auto shapeOffset = context->shapes.size();
  for (size_t i = 0; i < groupContext.shapes.size(); i++) {
    auto matrix = groupContext.matrices[i];
    matrix.postConcat(groupMatrix);
    context->shapes.push_back(std::move(groupContext.shapes[i]));
    context->matrices.push_back(matrix);
  }

  // Merge painters with index offset
  for (auto& painter : groupContext.painters) {
    painter->offsetShapeIndex(shapeOffset);
    painter->applyTransform(groupMatrix, _alpha);
    context->painters.push_back(std::move(painter));
  }
}

}  // namespace tgfx
