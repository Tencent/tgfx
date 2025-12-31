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
#include "gpu/QuadsVertexProvider.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

/**
 * Quads3DDrawArgs defines arguments for 3D quad rendering.
 */
struct Quads3DDrawArgs {
  /**
   * The transformation matrix from local space to clip space.
   */
  Matrix3D transformMatrix = Matrix3D::I();

  /**
   * The scaling and translation parameters in NDC space. After the projected model's vertex
   * coordinates are transformed to NDC, ndcScale is applied for scaling, followed by ndcOffset for
   * translation. These two properties allow any rectangular region of the projected model to be
   * mapped to any position within the target texture.
   */
  Vec2 ndcScale = Vec2(1.f, 1.f);
  Vec2 ndcOffset = Vec2(0.f, 0.f);

  /**
   * Reference viewport size, used to convert NDC coordinates to window coordinates. The external
   * transformMatrix, ndcScale, and ndcOffset are all defined based on this viewport size.
   */
  Size viewportSize = Size(1.f, 1.f);
};

/**
 * Quads3DDrawOp draws a batch of 3D quads with per-edge anti-aliasing support.
 */
class Quads3DDrawOp : public DrawOp {
 public:
  /**
   * Creates a new Quads3DDrawOp for the specified vertex provider.
   */
  static PlacementPtr<Quads3DDrawOp> Make(Context* context,
                                          PlacementPtr<QuadsVertexProvider> provider,
                                          uint32_t renderFlags, const Quads3DDrawArgs& drawArgs);

 private:
  Quads3DDrawOp(BlockAllocator* allocator, QuadsVertexProvider* provider,
                const Quads3DDrawArgs& drawArgs);

  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::Quads3DDrawOp;
  }

  Quads3DDrawArgs drawArgs;

  size_t quadCount = 0;
  std::optional<PMColor> commonColor = std::nullopt;

  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  friend class BlockAllocator;
};

}  // namespace tgfx
