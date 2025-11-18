/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "gpu/RRectsVertexProvider.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/VertexBufferView.h"

namespace tgfx {
class RRectDrawOp : public DrawOp {
 public:
  /**
   * The maximum number of round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 1024;

  /**
   * The maximum number of vertices per fill round rect.
   */
  static constexpr uint16_t IndicesPerFillRRect = 54;

  /**
   * The maximum number of vertices per stroke round rect.
   */
  static constexpr uint16_t IndicesPerStrokeRRect = 48;

  /**
   * Create a new RRectDrawOp for a list of RRect records. Note that the returned RRectDrawOp is in
   * the device space.
   */
  static PlacementPtr<RRectDrawOp> Make(Context* context,
                                        PlacementPtr<RRectsVertexProvider> provider,
                                        uint32_t renderFlags);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::RRectDrawOp;
  }

  bool hasCoverage() const override {
    return true;
  }

 private:
  size_t rectCount = 0;
  bool hasStroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  explicit RRectDrawOp(RRectsVertexProvider* provider);

  friend class BlockAllocator;
};
}  // namespace tgfx
