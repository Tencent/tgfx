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
#include "DrawOp.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/proxies/IndexBufferProxy.h"
#include "gpu/proxies/VertexBufferProxyView.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

/**
 * PerspectiveRenderArgs defines arguments for perspective rendering.
 */
struct Rect3DDrawArgs {
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

  /**
   * Reference viewport size, used to convert NDC coordinates to window coordinates. The external
   * transformMatrix, ndcScale, and ndcOffset are all defined based on this viewport size.
   */
  Size viewportSize;
};

class Rect3DDrawOp : public DrawOp {
 public:
  /**
 * Create a new RectDrawOp for the specified vertex provider.
 */
  static PlacementPtr<Rect3DDrawOp> Make(Context* context,
                                         PlacementPtr<RectsVertexProvider> provider,
                                         uint32_t renderFlags, const Rect3DDrawArgs& drawArgs);

 private:
  Rect3DDrawOp(RectsVertexProvider* provider, const Rect3DDrawArgs& drawArgs);

  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::Rect3DDrawOp;
  }

  Rect3DDrawArgs drawArgs;

  size_t rectCount = 0;
  std::optional<Color> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
  bool hasSubset = false;

  std::shared_ptr<IndexBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferProxyView> vertexBufferProxyView = nullptr;

  friend class BlockBuffer;
};

}  // namespace tgfx