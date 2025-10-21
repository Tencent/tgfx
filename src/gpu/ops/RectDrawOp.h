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
#include "gpu/RectsVertexProvider.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "gpu/proxies/VertexBufferView.h"

namespace tgfx {
class RectDrawOp : public DrawOp {
 public:
  /**
   * The maximum number of rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRects = 2048;

  /**
   * The maximum number of stroke rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumStrokeRects = 1024;

  /**
   * The maximum number of vertices per non-AA quad.
   */
  static constexpr uint16_t IndicesPerNonAAQuad = 6;

  /**
   * The maximum number of vertices per AA quad.
   */
  static constexpr uint16_t IndicesPerAAQuad = 30;

  /**
   * The number of indices per AA rect
   */
  static constexpr uint16_t IndicesPerAAStrokeRect = 3 * 24;

  /**
   * The number of indices per non-AA rect.
   */
  static constexpr uint16_t IndicesPerNonAAStrokeRect = 24;

  /**
   * Create a new RectDrawOp for the specified vertex provider.
   */
  static PlacementPtr<RectDrawOp> Make(Context* context, PlacementPtr<RectsVertexProvider> provider,
                                       uint32_t renderFlags);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::RectDrawOp;
  }

 private:
  size_t rectCount = 0;
  bool hasStroke = false;
  std::optional<Color> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
  bool hasSubset = false;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<VertexBufferView> vertexBufferProxyView = nullptr;

  explicit RectDrawOp(RectsVertexProvider* provider);

  friend class BlockBuffer;
};
}  // namespace tgfx
