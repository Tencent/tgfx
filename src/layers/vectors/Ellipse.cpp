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

#include "tgfx/layers/vectors/Ellipse.h"
#include "VectorContext.h"

namespace tgfx {

void Ellipse::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::setSize(const Point& value) {
  if (_size == value) {
    return;
  }
  _size = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    auto halfWidth = _size.x * 0.5f;
    auto halfHeight = _size.y * 0.5f;
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, _size.x, _size.y);
    Path path;
    path.addOval(rect, _reversed);
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
