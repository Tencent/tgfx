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
  setRoundness(std::array<float, 4>{value, value, value, value});
}

void Rectangle::setRoundness(const std::array<float, 4>& values) {
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
    // Replace zero-extent axes with a sub-pixel epsilon so a Rectangle with one or both
    // sides collapsed to zero stays a closed area shape. The value sits just above the
    // engine-wide FLOAT_NEARLY_ZERO (1/4096 ~ 2.44e-4) so stroker and path utility code
    // keep treating the axis as non-zero, yet remains far below any sampling grid (GPUs
    // sample at 1/16 px or coarser), so the geometry still reads as a line or point
    // visually while the stroker can expand symmetrically on both sides and trim, dash,
    // and inside/outside align all keep their well-defined area semantics.
    constexpr float MinExtent = 5e-4f;
    auto width = std::max(_size.width, MinExtent);
    auto height = std::max(_size.height, MinExtent);
    auto halfWidth = width * 0.5f;
    auto halfHeight = height * 0.5f;
    auto rect = Rect::MakeXYWH(_position.x - halfWidth, _position.y - halfHeight, width, height);
    Path path;
    std::array<Point, 4> radii = {{{_roundness[0], _roundness[0]},
                                   {_roundness[1], _roundness[1]},
                                   {_roundness[2], _roundness[2]},
                                   {_roundness[3], _roundness[3]}}};
    // MakeRectRadii scales the radii down proportionally if adjacent corners overflow any edge.
    auto rRect = RRect::MakeRectRadii(rect, radii);
    path.addRRect(rRect, _reversed, 2);
    _cachedShape = Shape::MakeFrom(path);
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
