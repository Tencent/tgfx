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

#include "layers/contents/GeometryContent.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

/**
 * DrawContent is the base class for geometry contents that store their own draw attributes.
 * Each DrawContent represents a single draw operation with its own color, shader, stroke and blend
 * mode.
 */
class DrawContent : public GeometryContent {
 public:
  /**
   * @param paint The paint that captured this geometry. Ignored when hasPaint is false: an entry no
   * paint captured holds no paint state, so any read of it would be a read of nothing.
   * @param hasPaint Whether a paint captured this geometry.
   */
  explicit DrawContent(const LayerPaint& paint, bool hasPaint);

  Rect getBounds(bool includeUnpaintedGeometry) const override;
  Rect getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const override = 0;
  bool hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const override = 0;
  bool contourEqualsOpaqueContent() const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override;
  const std::shared_ptr<Shader>& getShader() const override;
  bool hasSameGeometry(const GeometryContent* other) const override;
  bool hasBlendMode() const override;
  bool hasPaint() const override;
  bool hasPaintedContent() const override;

  const Color& getColor() const {
    return color;
  }

  const BlendMode& getBlendMode() const {
    return blendMode;
  }

  const Stroke* getStroke() const {
    return stroke.get();
  }

 protected:
  void drawContourInternal(Canvas* canvas, bool antiAlias,
                           bool includeUnpaintedGeometry) const override;

  virtual Rect onGetBounds() const = 0;
  virtual void onDraw(Canvas* canvas, const Paint& paint) const = 0;
  virtual bool onHasSameGeometry(const GeometryContent* other) const = 0;

  /**
   * Returns true when this entry must stay out of a query. An entry no paint captured paints
   * nothing, so it only takes part in a query that asks for it; a painter with alpha 0 keeps its
   * contour but paints nothing either, which the alpha test of the query itself covers.
   */
  bool isExcluded(bool includeUnpaintedGeometry) const {
    return !painted && !includeUnpaintedGeometry;
  }

  Color color = Color::White();
  std::shared_ptr<Shader> shader = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  std::unique_ptr<Stroke> stroke = nullptr;
  bool painted = true;
};

}  // namespace tgfx
