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
class RRectPaint;

class RRectOp : public DrawOp {
 public:
  DEFINE_OP_CLASS_ID

  /**
   * The maximum number of round rects that can be drawn in a single draw call.
   */
  static constexpr uint16_t MaxNumRRects = 1024;

  static std::unique_ptr<RRectOp> Make(Color color, const RRect& rRect, const Matrix& viewMatrix);

  void prepare(Context* context) override;

  void execute(RenderPass* renderPass) override;

 private:
  RRectOp(Color color, const RRect& rRect, const Matrix& viewMatrix, const Matrix& localMatrix);

  bool onCombineIfPossible(Op* op) override;

  std::vector<std::shared_ptr<RRectPaint>> rRectPaints;
  Matrix localMatrix = Matrix::I();
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy;

  //  bool stroked = false;
  //  Point strokeWidths = Point::Zero();
};
}  // namespace tgfx
