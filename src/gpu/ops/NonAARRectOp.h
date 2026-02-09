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
class NonAARRectsVertexProvider;

/**
 * NonAARRectOp draws round rectangles without antialiasing. It uses a simplified
 * vertex layout compared to RRectDrawOp, suitable for non-AA rendering only.
 * Supports both fill and stroke modes.
 */
class NonAARRectOp : public DrawOp {
 public:
  /**
   * The maximum number of round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 1024;

  /**
   * The number of vertices per round rect.
   * 4 corner vertices for a simple quad.
   */
  static constexpr uint16_t VerticesPerRRect = 4;

  /**
   * The number of indices per round rect.
   * 6 indices for 2 triangles forming a quad.
   */
  static constexpr uint16_t IndicesPerRRect = 6;

  /**
   * Create a new NonAARRectOp for a list of RRect records.
   */
  static PlacementPtr<NonAARRectOp> Make(Context* context,
                                         PlacementPtr<NonAARRectsVertexProvider> provider,
                                         uint32_t renderFlags);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::NonAARRectOp;
  }

  bool hasCoverage() const override {
    return false;
  }

 private:
  size_t rectCount = 0;
  bool hasStroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  NonAARRectOp(BlockAllocator* allocator, NonAARRectsVertexProvider* provider);

  friend class BlockAllocator;
};
}  // namespace tgfx
