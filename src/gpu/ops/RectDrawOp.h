/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/ops/DrawOp.h"

namespace tgfx {
struct RectPaint {
  RectPaint(const Rect& rect, const Matrix& viewMatrix, const Color& color = Color::White())
      : rect(rect), viewMatrix(viewMatrix), color(color) {
  }

  Rect rect;
  Matrix viewMatrix;
  Color color;
};

class RectDrawOp : public DrawOp {
 public:
  /**
   * The maximum number of rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRects = 2048;

  /**
   * Create a new RectDrawOp for a list of RectPaints. The returned RectDrawOp is in the local space
   * of each rect.
   */
  static PlacementPtr<RectDrawOp> Make(Context* context, std::vector<PlacementPtr<RectPaint>> rects,
                                       bool needUVCoord, AAType aaType, uint32_t renderFlags);

  void execute(RenderPass* renderPass) override;

 private:
  size_t rectCount = 0;
  std::optional<Color> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  size_t vertexBufferOffset = 0;

  RectDrawOp(AAType aaType, size_t rectCount);

  friend class BlockBuffer;
};
}  // namespace tgfx
