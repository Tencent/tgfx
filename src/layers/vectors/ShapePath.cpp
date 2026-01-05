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

#include "tgfx/layers/vectors/ShapePath.h"
#include "VectorContext.h"

namespace tgfx {

void ShapePath::setPath(Path value) {
  if (_path == value) {
    return;
  }
  _path = std::move(value);
  _cachedShape = nullptr;
  invalidateContent();
}

void ShapePath::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void ShapePath::apply(VectorContext* context) {
  if (_cachedShape == nullptr) {
    _cachedShape = Shape::MakeFrom(_path);
    if (_reversed) {
      _cachedShape = Shape::ApplyReverse(_cachedShape);
    }
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
