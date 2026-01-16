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
#include "core/utils/Log.h"

namespace tgfx {

void Ellipse::setCenter(const Point& value) {
  if (_center == value) {
    return;
  }
  _center = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Ellipse::setSize(const Size& value) {
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
  DEBUG_ASSERT(context != nullptr);
  if (_cachedShape == nullptr) {
    auto halfWidth = _size.width * 0.5f;
    auto halfHeight = _size.height * 0.5f;
    auto rect =
        Rect::MakeXYWH(_center.x - halfWidth, _center.y - halfHeight, _size.width, _size.height);
    Path path;
    path.addOval(rect, _reversed);
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
