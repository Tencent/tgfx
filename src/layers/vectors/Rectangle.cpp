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

#include "tgfx/layers/vectors/Rectangle.h"
#include <algorithm>
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Path.h"

namespace tgfx {

std::shared_ptr<Rectangle> Rectangle::Make() {
  return std::shared_ptr<Rectangle>(new Rectangle());
}

void Rectangle::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Rectangle::setSize(const Size& value) {
  if (_size == value) {
    return;
  }
  _size = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Rectangle::setRoundness(float value) {
  if (_roundness == value) {
    return;
  }
  _roundness = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Rectangle::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Rectangle::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_cachedShape == nullptr) {
    auto halfWidth = _size.width * 0.5f;
    auto halfHeight = _size.height * 0.5f;
    Path path;
    // A degenerate rectangle is treated as an open line segment (or a single point when both
    // sides are zero).
    bool degenerate = _size.width == 0.0f || _size.height == 0.0f;
    if (degenerate) {
      Point p0 = {_position.x - halfWidth, _position.y - halfHeight};
      Point p1 = {_position.x + halfWidth, _position.y + halfHeight};
      if (_reversed) {
        std::swap(p0, p1);
      }
      path.moveTo(p0);
      path.lineTo(p1);
    } else {
      auto radius = _roundness;
      if (radius > halfWidth) {
        radius = halfWidth;
      }
      if (radius > halfHeight) {
        radius = halfHeight;
      }
      auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, _size.width,
                                 _size.height);
      path.addRoundRect(rect, radius, radius, _reversed, 2);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
