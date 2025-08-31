/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#pragma once

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"

namespace tgfx {
class Clip {
  public:
  Clip() = default;

  friend bool operator==(const Clip& a, const Clip& b) {
    return a.path == b.path && a.forceAntiAlias == b.forceAntiAlias;
  }

  friend bool operator!=(const Clip& a, const Clip& b) {
    return a.path != b.path || a.forceAntiAlias != b.forceAntiAlias;
  }

  bool isSame(const Clip& other) const {
    return path.isSame(other.path) && forceAntiAlias == other.forceAntiAlias;
  }

  bool isEmpty() const {
    return path.isEmpty();
  }

  bool isInverseFillType() const {
    return path.isInverseFillType();
  }

  Path path = {};

  bool forceAntiAlias = false;
};

class MCState {
 public:
  explicit MCState(const Matrix& matrix) : matrix(matrix) {
    clip.path.toggleInverseFillType();
  }

  explicit MCState(Clip initClip) : clip(std::move(initClip)) {
  }

  MCState(const Matrix& matrix, Clip clip) : matrix(matrix), clip(std::move(clip)) {
  }

  MCState() {
    clip.path.toggleInverseFillType();
  }

  Matrix matrix = {};
  Clip clip = {};
};
}  // namespace tgfx
