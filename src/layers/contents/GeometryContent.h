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
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {

/**
 * GeometryContent is the base class for geometry-based layer contents. Each GeometryContent
 * represents a single draw operation with its own color, shader, and blend mode.
 */
class GeometryContent : public LayerContent {
 public:
  explicit GeometryContent(const LayerPaint& paint);

  Rect getBounds() const override;
  bool contourEqualsOpaqueContent() const override;
  void drawContour(Canvas* canvas, bool antiAlias) const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override;

  /**
   * Returns true if this content has the same geometry as the other content.
   */
  bool hasSameGeometry(const GeometryContent* other) const;

  Color color = Color::White();
  std::shared_ptr<Shader> shader = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  std::unique_ptr<Stroke> stroke = nullptr;

 protected:
  virtual Rect onGetBounds() const = 0;
  virtual void onDraw(Canvas* canvas, const Paint& paint) const = 0;
  virtual bool onHasSameGeometry(const GeometryContent* other) const = 0;
};

}  // namespace tgfx
