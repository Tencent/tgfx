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

#include <optional>
#include "DrawOp.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/QuadsVertexProvider.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

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
                                          uint32_t renderFlags);

 private:
  Quads3DDrawOp(BlockAllocator* allocator, QuadsVertexProvider* provider);

  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::Quads3DDrawOp;
  }

  size_t quadCount = 0;
  std::optional<PMColor> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;

  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  friend class BlockAllocator;
};

}  // namespace tgfx
