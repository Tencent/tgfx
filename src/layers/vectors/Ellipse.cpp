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
#include <algorithm>
#include "VectorContext.h"
#include "core/utils/Log.h"

namespace tgfx {

static constexpr float ELLIPSE_MIN_EXTENT = 5e-3f;

std::shared_ptr<Ellipse> Ellipse::Make() {
  return std::shared_ptr<Ellipse>(new Ellipse());
}

void Ellipse::setPosition(const Point& value) {
  if (_position == value) {
    return;
  }
  _position = value;
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
    // Clamp to a non-zero extent so the generated oval remains a well-defined path with
    // valid tangents and length for downstream stroke/dash/measure operations. The value sits
    // above the stroker's internal precision threshold so each edge is not treated as a tiny
    // segment, yet far below any sampling grid so the shape stays visually invisible.
    const auto width = std::max(_size.width, ELLIPSE_MIN_EXTENT);
    const auto height = std::max(_size.height, ELLIPSE_MIN_EXTENT);
    const auto halfWidth = width * 0.5f;
    const auto halfHeight = height * 0.5f;
    const auto rect =
        Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, width, height);
    Path path;
    path.addOval(rect, _reversed);
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
