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

#include <vector>
#include "tgfx/core/Path.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * Defines how multiple paths are trimmed.
 */
enum class TrimPathType {
  /**
   * Trim all paths as one continuous path.
   */
  Simultaneously,
  /**
   * Trim each path individually.
   */
  Individually
};

/**
 * TrimPath trims paths in the same group to a specified range.
 */
class TrimPath : public VectorElement {
 public:
  TrimPath() = default;

  /**
   * Returns the start of the trim range (0.0 to 1.0).
   */
  float start() const {
    return _start;
  }

  /**
   * Sets the start of the trim range.
   */
  void setStart(float value);

  /**
   * Returns the end of the trim range (0.0 to 1.0).
   */
  float end() const {
    return _end;
  }

  /**
   * Sets the end of the trim range.
   */
  void setEnd(float value);

  /**
   * Returns the offset of the trim range in degrees.
   */
  float offset() const {
    return _offset;
  }

  /**
   * Sets the offset of the trim range in degrees.
   */
  void setOffset(float value);

  /**
   * Returns how multiple paths are trimmed.
   */
  TrimPathType trimType() const {
    return _trimType;
  }

  /**
   * Sets how multiple paths are trimmed.
   */
  void setTrimType(TrimPathType value);

 protected:
  Type type() const override {
    return Type::TrimPath;
  }

  void apply(VectorContext* context) override;

 private:
  float _start = 0.0f;
  float _end = 1.0f;
  float _offset = 0.0f;
  TrimPathType _trimType = TrimPathType::Simultaneously;
};

}  // namespace tgfx
