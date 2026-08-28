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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/layers/PathMask.h"
#include <algorithm>

namespace tgfx {

std::shared_ptr<PathMask> PathMask::Make() {
  return std::shared_ptr<PathMask>(new PathMask());
}

void PathMask::setPath(const Path& path) {
  if (_path == path) {
    return;
  }
  _path = path;
  invalidateContent();
}

void PathMask::setOp(PathOp op) {
  if (_op == op) {
    return;
  }
  _op = op;
  invalidateContent();
}

void PathMask::setInverted(bool value) {
  if (_inverted == value) {
    return;
  }
  _inverted = value;
  invalidateContent();
}

void PathMask::setFeather(const Point& value) {
  if (_feather == value) {
    return;
  }
  _feather = value;
  invalidateContent();
}

void PathMask::setOpacity(float value) {
  auto clampedValue = std::clamp(value, 0.0f, 1.0f);
  if (_opacity == clampedValue) {
    return;
  }
  _opacity = clampedValue;
  invalidateContent();
}

void PathMask::setExpansion(float value) {
  if (_expansion == value) {
    return;
  }
  _expansion = value;
  invalidateContent();
}

}  // namespace tgfx
