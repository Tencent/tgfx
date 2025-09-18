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

#include "core/Matrix3D.h"
#include "gpu/AAType.h"
#include "gpu/proxies/IndexBufferProxy.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/VertexBufferProxyView.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {

/**
 * PerspectiveRenderArgs defines arguments for perspective rendering.
 */
struct PerspectiveRenderArgs {
  /**
   * Anti-aliasing type.
   */
  AAType aa = AAType::None;

  /**
   * The transformation matrix from local space to clip space.
   */
  Matrix3D transformMatrix;

  /**
   * The scaling and translation parameters in NDC space. After the projected model's vertex
   * coordinates are transformed to NDC, ndcScale is applied for scaling, followed by ndcOffset for
   * translation. These two properties allow any rectangular region of the projected model to be
   * mapped to any position within the target texture.
   */
  Vec2 ndcScale;
  Vec2 ndcOffset;
};

/**
 * RectPerspectiveRenderTask is a render task that renders a rectangle with perspective transformation.
 */
class RectPerspectiveRenderTask final : public RenderTask {
 public:
  /**
   * Creates a RectPerspectiveRenderTask with rect, render target, fill texture and render args.
   */
  RectPerspectiveRenderTask(const Rect& rect, std::shared_ptr<RenderTargetProxy> renderTarget,
                            std::shared_ptr<TextureProxy> fillTexture,
                            const PerspectiveRenderArgs& args);

  void execute(CommandEncoder* encoder) override;

 private:
  Rect rect;

  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;

  std::shared_ptr<TextureProxy> fillTexture = nullptr;

  PerspectiveRenderArgs args;

  std::shared_ptr<VertexBufferProxyView> vertexBufferProxyView = nullptr;

  std::shared_ptr<IndexBufferProxy> indexBufferProxy = nullptr;
};

}  // namespace tgfx