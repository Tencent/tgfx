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

#include <vector>
#include "Layer3DContext.h"

namespace tgfx {

class DrawArgs;

/**
 * Simplified 3D context for contour rendering. Unlike Render3DContext, this class does not run
 * BSP depth sorting; nodes are rasterized in registration order (layer-tree pre-order) and drawn
 * sequentially with their accumulated 3D transform applied directly. Contour output is always
 * single-channel alpha so paint order alone yields the correct result.
 */
class Opaque3DContext : public Layer3DContext {
 public:
  Opaque3DContext(const Rect& renderRect, float contentScale,
                  std::shared_ptr<ColorSpace> colorSpace);

  void addLayer(Layer* layer, const Matrix3D& transform, float alpha,
                LayerDrawFunc drawFunc) override;

  void finishAndDrawTo(const DrawArgs& args, Canvas* canvas) override;

 private:
  struct PendingNode {
    Layer* layer = nullptr;
    Matrix3D transform = {};
    Rect localBounds = {};
    float alpha = 1.0f;
  };

  void collectNodes(Layer* layer, const Matrix3D& transform, float alpha);

  std::vector<PendingNode> _pendingNodes = {};
  LayerDrawFunc _drawFunc = nullptr;
};

}  // namespace tgfx
