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

#include <memory>
#include "layers/contents/LayerContent.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

class Shader;

/**
 * GeometryContent is the abstract base class for geometry-based layer contents.
 */
class GeometryContent : public LayerContent {
 public:
  bool hitTestPoint(float localX, float localY) const override {
    return hitTestPoint(localX, localY, nullptr);
  }

  Rect getTightBounds(const Matrix& matrix) const override {
    return getTightBounds(matrix, nullptr);
  }

  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override {
    return drawDefault(canvas, alpha, antiAlias, nullptr);
  }

  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override {
    drawForeground(canvas, alpha, antiAlias, nullptr);
  }

  void drawContour(Canvas* canvas, bool antiAlias) const override {
    drawContour(canvas, antiAlias, nullptr);
  }

  /**
   * Returns true if this content has the same geometry as the other content.
   */
  virtual bool hasSameGeometry(const GeometryContent* other) const = 0;

  /**
   * Returns true if the geometry may contain sharp corners.
   */
  virtual bool mayHaveSharpCorners() const = 0;

  /**
   * Returns the fill color of this content.
   */
  virtual const Color& getColor() const = 0;

  /**
   * Returns the shader of this content.
   */
  virtual const std::shared_ptr<Shader>& getShader() const = 0;

  /**
   * Returns the blend mode of this content.
   */
  virtual const BlendMode& getBlendMode() const = 0;

  /**
   * Returns the tight bounds of the content mapped by Matrix.
   * @param stroke Optional stroke to apply.
   */
  virtual Rect getTightBounds(const Matrix& matrix, const Stroke* stroke) const = 0;

  /**
   * Returns true if the point (localX, localY) is inside the content.
   * @param stroke Optional stroke to apply.
   */
  virtual bool hitTestPoint(float localX, float localY, const Stroke* stroke) const = 0;

  /**
   * Draws the default content with the specified alpha and antiAlias.
   * @param stroke Optional stroke to apply.
   */
  virtual bool drawDefault(Canvas* canvas, float alpha, bool antiAlias,
                           const Stroke* stroke) const = 0;

  /**
   * Draws the foreground content with the specified alpha and antiAlias.
   * @param stroke Optional stroke to apply.
   */
  virtual void drawForeground(Canvas* canvas, float alpha, bool antiAlias,
                              const Stroke* stroke) const = 0;

  /**
   * Draws the contour of the content.
   * @param stroke Optional stroke to apply.
   */
  virtual void drawContour(Canvas* canvas, bool antiAlias, const Stroke* stroke) const = 0;
};

}  // namespace tgfx
