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
   * The maximum number of non-AA rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumNonAARects = 2048;  // max possible: (1 << 14) - 1;

  /**
   * The maximum number of AA rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumAARects = 512;  // max possible: (1 << 13) - 1;

  /**
   * Create a new RectDrawOp for a list of RectPaints. The returned RectDrawOp is in the local space
   * of each rect.
   */
  static PlacementNode<RectDrawOp> Make(Context* context, PlacementList<RectPaint> rects,
                                        bool useUVCoord, AAType aaType, uint32_t renderFlags);

  RectDrawOp(AAType aaType, size_t rectCount, bool useUVCoord);

  void execute(RenderPass* renderPass) override;

 private:
  size_t rectCount = 0;
  std::optional<Color> uniformColor = std::nullopt;
  bool useUVCoord = false;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  std::shared_ptr<Data> vertexData = nullptr;
};
}  // namespace tgfx
