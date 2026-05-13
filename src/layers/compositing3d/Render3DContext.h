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

#include <memory>
#include <vector>
#include "Layer3DContext.h"
#include "tgfx/core/Image.h"

namespace tgfx {

class Context3DCompositor;
class DrawArgs;

/**
 * Render3DContext composites layers using BSP-based depth sorting. addLayer recursively expands
 * the preserve3D subtree, collecting one node per registered layer. finishAndDrawTo builds a BSP
 * tree, rasterizes each layer (once per Layer* on demand) by invoking Layer::drawLayer, then
 * composites fragments back-to-front.
 */
class Render3DContext : public Layer3DContext {
 public:
  Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Rect& renderRect,
                  float contentScale, std::shared_ptr<ColorSpace> colorSpace);

  void addLayer(Layer* layer, const Matrix3D& transform, float alpha,
                LayerDrawFunc drawFunc) override;

  void finishAndDrawTo(const DrawArgs& args, Canvas* canvas) override;

 private:
  struct PendingNode {
    Layer* layer = nullptr;
    Matrix3D transform = {};
    Rect localBounds = {};
    int depth = 0;
    float alpha = 1.0f;
    bool antialiasing = true;
  };

  void collectNodes(Layer* layer, const Matrix3D& transform, float alpha, int depth);
  std::shared_ptr<Image> rasterLayer(Layer* layer, const Rect& localBounds, float alpha,
                                     DrawArgs& leafArgs);

  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  std::vector<PendingNode> _pendingNodes = {};
  LayerDrawFunc _drawFunc = nullptr;
};

}  // namespace tgfx
