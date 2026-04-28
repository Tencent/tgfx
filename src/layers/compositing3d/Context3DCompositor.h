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

#include <deque>
#include "DrawPolygon3D.h"
#include "DrawableRect.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

class BackgroundContext;

/**
 * Context3DCompositor handles compositing of 3D transformed images using BSP tree for correct
 * depth sorting. It splits intersecting regions to ensure correct occlusion and blending order.
 */
class Context3DCompositor {
 public:
  /**
   * Constructs a compositor for 3D layer compositing.
   * @param context The GPU context.
   * @param renderRect The rendering area in the scaled canvas coordinate system.
   * @param contentScale The scale factor applied to layer content.
   * @param colorSpace The color space of the composited result.
   * @param backgroundContext The background context for background styles, or nullptr.
   */
  Context3DCompositor(const Context& context, const Rect& renderRect, float contentScale,
                      std::shared_ptr<ColorSpace> colorSpace,
                      std::shared_ptr<BackgroundContext> backgroundContext);

  int width() const {
    return static_cast<int>(_renderRect.width());
  }

  int height() const {
    return static_cast<int>(_renderRect.height());
  }

  /**
   * Adds a DrawableRect for compositing. The compositor takes ownership of the rect's lifecycle.
   * @param drawRect The draw data to add. Must have a valid image.
   */
  void addDrawRect(std::unique_ptr<DrawableRect> drawRect);

  /**
   * Draws all added rects with correct depth ordering and blending.
   * @return The composited image.
   */
  std::shared_ptr<Image> finish();

 private:
  void drawPolygon(const DrawPolygon3D* polygon);
  void drawQuads(const DrawPolygon3D* polygon, const std::vector<Quad>& subQuads);
  void drawBackgroundStyles(const DrawPolygon3D* polygon);
  void drawBackgroundStyle(const DrawPolygon3D* polygon, LayerStyle& style,
                           const std::shared_ptr<Image>& mask, const Point& maskOffset,
                           const std::shared_ptr<Image>& bgImage, const Point& bgOffset);
  void syncToBackgroundContext(const DrawPolygon3D* polygon) const;
  std::shared_ptr<Image> getBackgroundImage(const DrawableRect& drawRect, Point* offset) const;

  Rect _renderRect = {};
  float _contentScale = 1.0f;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::shared_ptr<RenderTargetProxy> _targetColorProxy = nullptr;
  std::shared_ptr<BackgroundContext> _backgroundContext = nullptr;
  std::vector<std::unique_ptr<DrawableRect>> _drawRects = {};
  std::deque<std::unique_ptr<DrawPolygon3D>> _polygons = {};
  std::vector<PlacementPtr<DrawOp>> _drawOps = {};
};

}  // namespace tgfx
