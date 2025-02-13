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
   * Create a new RRectDrawOp for a list of RRectPaints. Note that the returned RRectDrawOp is in
   * the device space.
   */
  static std::unique_ptr<RRectDrawOp> Make(Context* context, const std::vector<RRectPaint>& rects,
                                           AAType aaType, uint32_t renderFlags);

  void execute(RenderPass* renderPass) override;

 private:
  RRectDrawOp(const Rect& bounds, AAType aaType, size_t rectCount);

  size_t rectCount = 0;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  std::shared_ptr<Data> vertexData = nullptr;
};
}  // namespace tgfx
