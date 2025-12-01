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

namespace tgfx {
class Brush;

/**
 * BrushModifier is an interface for modifying Brush properties before they are applied in drawing
 * operations. It allows dynamic adjustment of attributes such as color, alpha, or filters.
 */
class BrushModifier {
 public:
  virtual ~BrushModifier() = default;

  /**
   * Transforms the given Brush and returns a new Brush with modifications applied.
   */
  virtual Brush transform(const Brush& brush) const = 0;
};
}  // namespace tgfx
