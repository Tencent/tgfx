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
#include "tgfx/core/PathEffect.h"
#include "tgfx/layers/vectors/VectorElement.h"

namespace tgfx {

/**
 * RoundCorner rounds the corners of paths in the same group.
 */
class RoundCorner : public VectorElement {
 public:
  RoundCorner() = default;

  /**
   * Returns the radius of the rounded corners.
   */
  float radius() const {
    return _radius;
  }

  /**
   * Sets the radius of the rounded corners.
   */
  void setRadius(float value);

 protected:
  Type type() const override {
    return Type::RoundCorner;
  }

  void apply(VectorContext* context) override;

 private:
  float _radius = 0.0f;
  std::shared_ptr<PathEffect> _cachedEffect = nullptr;
};

}  // namespace tgfx
