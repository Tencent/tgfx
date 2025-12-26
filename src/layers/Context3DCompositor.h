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

#include <deque>
#include "layers/DrawPolygon3D.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Path.h"

namespace tgfx {

/**
 * Context3DCompositor handles compositing of 3D transformed images using BSP tree for correct
 * depth sorting. It splits intersecting regions to ensure correct occlusion and blending order.
 */
class Context3DCompositor {
 public:
  Context3DCompositor(const Context& context, int width, int height);

  /**
   * Adds an image with 3D transformation for compositing.
   * @param image The source image to draw.
   * @param matrix The 3D transformation matrix applied to the image.
   * @param alpha The layer alpha for transparency.
   * @param antiAlias Whether to enable edge antialiasing when the render target does not support MSAA.
   */
  void addImage(std::shared_ptr<Image> image, const Matrix3D& matrix, float alpha, bool antiAlias);

  /**
   * Draws all added images with correct depth ordering and blending.
   * @return The composited image.
   */
  std::shared_ptr<Image> finish();

 private:
  void drawPolygon(const DrawPolygon3D* polygon);
  Path buildClipPath(const std::vector<Vec3>& points);
  std::shared_ptr<TextureProxy> getClipTexture(Context* context, const Path& clipPath);
  std::pair<PlacementPtr<FragmentProcessor>, Rect> getClipMaskFP(Context* context,
                                                                 const Path& clipPath);

  int _width = 0;
  int _height = 0;
  std::shared_ptr<RenderTargetProxy> _targetColorProxy = nullptr;
  std::deque<std::unique_ptr<DrawPolygon3D>> _polygons = {};
  std::vector<PlacementPtr<DrawOp>> _drawOps = {};
  int _nextOrderIndex = 0;
};

}  // namespace tgfx
