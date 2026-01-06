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

#pragma once

#include "tgfx/core/Path.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * ShapePath represents a custom path shape.
 */
class ShapePath : public VectorElement {
 public:
  ShapePath() = default;

  /**
   * Returns the path that defines the shape.
   */
  const Path& path() const {
    return _path;
  }

  /**
   * Sets the path that defines the shape.
   */
  void setPath(Path value);

  /**
   * Returns whether the path direction is reversed (counter-clockwise).
   */
  bool reversed() const {
    return _reversed;
  }

  /**
   * Sets whether the path direction is reversed.
   */
  void setReversed(bool value);

 protected:
  Type type() const override {
    return Type::ShapePath;
  }

  void apply(VectorContext* context) override;

 private:
  Path _path = {};
  bool _reversed = false;
  std::shared_ptr<Shape> _cachedShape = nullptr;
};

}  // namespace tgfx
