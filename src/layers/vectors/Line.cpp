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

#include "tgfx/layers/vectors/Line.h"
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Path.h"

namespace tgfx {

std::shared_ptr<Line> Line::Make() {
  return std::shared_ptr<Line>(new Line());
}

void Line::setStartPoint(const Point& value) {
  if (_startPoint == value) {
    return;
  }
  _startPoint = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Line::setEndPoint(const Point& value) {
  if (_endPoint == value) {
    return;
  }
  _endPoint = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Line::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Line::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_cachedShape == nullptr) {
    Path path;
    if (_reversed) {
      path.moveTo(_endPoint);
      path.lineTo(_startPoint);
    } else {
      path.moveTo(_startPoint);
      path.lineTo(_endPoint);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
