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
  std::array<float, 4> values = {value, value, value, value};
  if (_roundness == values) {
    return;
  }
  _roundness = values;
  _cachedShape = nullptr;
  invalidateContent();
}

void Rectangle::setCornerRoundness(const std::array<float, 4>& values) {
  if (_roundness == values) {
    return;
  }
  _roundness = values;
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
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, _size.width,
                               _size.height);
    Path path;
    if (_roundness[0] == _roundness[1] && _roundness[1] == _roundness[2] &&
        _roundness[2] == _roundness[3]) {
      auto r = std::min({_roundness[0], halfWidth, halfHeight});
      path.addRoundRect(rect, r, r, _reversed, 2);
    } else {
      std::array<Point, 4> radii = {};
      for (size_t i = 0; i < 4; ++i) {
        auto r = std::min({_roundness[i], halfWidth, halfHeight});
        radii[i] = {r, r};
      }
      path.addRoundRect(rect, radii, _reversed, 2);
    }
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
