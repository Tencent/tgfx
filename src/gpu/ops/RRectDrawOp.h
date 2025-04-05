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

#include "DrawOp.h"
#include "tgfx/core/Path.h"

namespace tgfx {
struct RRectPaint {
  RRectPaint(const RRect& rRect, const Matrix& viewMatrix, Color color = Color::White())
      : rRect(rRect), viewMatrix(viewMatrix), color(color) {
  }

  RRect rRect;
  Matrix viewMatrix;
  Color color;
};

class RRectDrawOp : public DrawOp {
 public:
  /**
   * The maximum number of round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 512;

  /**
   * Create a new RRectDrawOp for a list of RRectPaints. Note that the returned RRectDrawOp is in
   * the device space.
   */
  static PlacementPtr<RRectDrawOp> Make(Context* context,
                                        std::vector<PlacementPtr<RRectPaint>> rects, AAType aaType,
                                        uint32_t renderFlags);

  RRectDrawOp(AAType aaType, size_t rectCount);

  void execute(RenderPass* renderPass) override;

 private:
  size_t rectCount = 0;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  size_t vertexBufferOffset = 0;
};
}  // namespace tgfx
