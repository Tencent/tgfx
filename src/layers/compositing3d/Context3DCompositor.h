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
#include <unordered_map>
#include "DrawPolygon3D.h"
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
   * Adds an image with 3D transformation for compositing.
   * @param sourceLayer The source layer this image was created from. Must not be nullptr.
   * @param contentOffset The offset of the content image in the layer's scaled local space.
   * @param image The source image to draw.
   * @param matrix The 3D transformation matrix applied to the image.
   * @param depth The depth level in the layer tree (used for sorting coplanar polygons).
   * @param alpha The layer alpha for transparency.
   * @param antiAlias Whether to enable edge antialiasing when the render target does not support MSAA.
   */
  void addImage(Layer* sourceLayer, const Point& contentOffset, std::shared_ptr<Image> image,
                const Matrix3D& matrix, int depth, float alpha, bool antiAlias);

  /**
   * Draws all added images with correct depth ordering and blending.
   * @return The composited image.
   */
  std::shared_ptr<Image> finish();

 private:
  void drawPolygon(const DrawPolygon3D* polygon);
  void drawQuads(const DrawPolygon3D* polygon, const std::vector<Quad>& subQuads);
  void drawBackgroundStyles(const DrawPolygon3D* polygon);
  void syncToBackgroundContext(const DrawPolygon3D* polygon);
  std::shared_ptr<Image> getBackgroundImage(Layer& layer, Point* offset);
  std::shared_ptr<Image> getContentImage(Layer& layer);

  Rect _renderRect = {};
  float _contentScale = 1.0f;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::shared_ptr<RenderTargetProxy> _targetColorProxy = nullptr;
  std::shared_ptr<BackgroundContext> _backgroundContext = nullptr;
  std::deque<std::unique_ptr<DrawPolygon3D>> _polygons = {};
  std::vector<PlacementPtr<DrawOp>> _drawOps = {};
  std::unordered_map<int, int> _depthSequenceCounters = {};
};

}  // namespace tgfx
