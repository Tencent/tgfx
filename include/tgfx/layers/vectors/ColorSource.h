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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
/**
 * ColorSource specifies the source color(s) for what is being drawn in a vector layer. There are
 * three types of ColorSource: SolidColor, Gradient, and ImagePattern. Note: All ColorSource objects
 * are not thread-safe and should only be accessed from a single thread.
 */
class ColorSource : public LayerProperty {
 public:
  /**
   * Returns the shader that generates colors for drawing.
   */
  virtual std::shared_ptr<Shader> getShader() const = 0;

  /**
   * Returns true when this ColorSource's parameters are interpreted relative to each geometry's
   * bounding box. When true, FillStyle/StrokeStyle supplies the shader with a per-geometry fit
   * matrix obtained from getFitMatrix(). When false, the shader is used as-is in the layer's
   * coordinate space.
   */
  virtual bool fitsToGeometry() const = 0;

  /**
   * Returns the transformation matrix that maps the ColorSource's local coordinate space into the
   * given geometry bounding box. Only consulted when fitsToGeometry() returns true.
   * @param bounds The bounding box of the target geometry.
   */
  virtual Matrix getFitMatrix(const Rect& bounds) const = 0;

 protected:
  enum class Type { Gradient, ImagePattern, SolidColor };

  virtual Type getType() const = 0;

 private:
  friend class Types;
  friend class FillStyle;
  friend class StrokeStyle;
};
}  // namespace tgfx
