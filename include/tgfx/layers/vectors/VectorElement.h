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

#pragma once

#include "tgfx/layers/LayerProperty.h"

namespace tgfx {

struct VectorContext;

/**
 * VectorElement is the base class for all vector elements in a shape layer. It includes shapes
 * (rect, ellipse, path, etc.), modifiers (fill, stroke, trim path, etc.), and groups.
 */
class VectorElement : public LayerProperty {
 public:
  /**
   * Returns whether this element is enabled for rendering.
   */
  bool enabled() const {
    return _enabled;
  }

  /**
   * Sets whether this element is enabled for rendering.
   */
  void setEnabled(bool value);

 protected:
  enum class Type {
    Rectangle,
    Ellipse,
    Polystar,
    ShapePath,
    FillStyle,
    StrokeStyle,
    TrimPath,
    RoundCorner,
    MergePath,
    Repeater,
    VectorGroup
  };

  VectorElement() = default;

  virtual Type type() const = 0;

  /**
   * Applies this element's effect to the given context. Geometry elements add paths,
   * modifiers transform paths, and styles render the accumulated paths.
   */
  virtual void apply(VectorContext* context) = 0;

 private:
  bool _enabled = true;

  friend class VectorLayer;
  friend class VectorGroup;
};

}  // namespace tgfx
