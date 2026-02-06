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

#include <optional>
#include "DrawOp.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/VertexBufferView.h"

namespace tgfx {
class FillRRectsVertexProvider;

class FillRRectOp : public DrawOp {
 public:
  /**
   * The maximum number of fill round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 1024;

  /**
   * The number of vertices per fill round rect.
   * 8 inset vertices + 8 outset vertices + 24 corner vertices = 40 vertices
   */
  static constexpr uint16_t VerticesPerRRect = 40;

  /**
   * The number of indices per fill round rect.
   * 18 inset octagon indices + 24 AA border indices + 48 corner indices = 90 indices
   */
  static constexpr uint16_t IndicesPerRRect = 90;

  /**
   * Create a new FillRRectOp for a list of fill RRect records.
   */
  static PlacementPtr<FillRRectOp> Make(Context* context,
                                        PlacementPtr<FillRRectsVertexProvider> provider,
                                        uint32_t renderFlags);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::FillRRectOp;
  }

  bool hasCoverage() const override {
    return aaType == AAType::Coverage;
  }

 private:
  size_t rectCount = 0;
  std::optional<PMColor> commonColor = std::nullopt;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  FillRRectOp(BlockAllocator* allocator, FillRRectsVertexProvider* provider);

  friend class BlockAllocator;
};
}  // namespace tgfx
