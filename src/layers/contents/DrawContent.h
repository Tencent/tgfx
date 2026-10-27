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

#include <optional>
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
   * @param paint The paint that captured this geometry, or nullptr when no paint did.
   */
  explicit DrawContent(const LayerPaint* paint);

  Rect getBounds(bool includeUnpaintedGeometry) const override;
  Rect getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const override = 0;
  bool hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const override = 0;
  bool contourEqualsOpaqueContent(bool includeUnpaintedGeometry) const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override;
  const std::shared_ptr<Shader>& getShader() const override;
  bool hasSameGeometry(const GeometryContent* other) const override;
  bool hasBlendMode() const override;
  bool hasPaint() const override;

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
   * Returns true when this entry must stay out of a query: an entry no paint captured is excluded
   * unless the query asks for it. A painter with alpha 0 stays in; isHiddenFromHitTest() is what
   * drops it from a point query.
   */
  bool isExcluded(bool includeUnpaintedGeometry) const {
    return !hasPaint() && !includeUnpaintedGeometry;
  }

  /**
   * Returns true when this entry must stay out of a point query: an entry no paint captured is only
   * hit-testable when the query asks for it, and a painter whose alpha is 0 never hits.
   */
  bool isHiddenFromHitTest(bool includeUnpaintedGeometry) const {
    if (!color.has_value()) {
      return !includeUnpaintedGeometry;
    }
    return color->alpha <= 0;
  }

  /// Nullopt when no paint captured this geometry; a transparent painter is a value with alpha 0.
  std::optional<Color> color = std::nullopt;
  std::shared_ptr<Shader> shader = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  std::unique_ptr<Stroke> stroke = nullptr;
};

}  // namespace tgfx
