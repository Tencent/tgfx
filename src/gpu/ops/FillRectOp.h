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
class FillRectOp : public DrawOp {
 public:
  static std::unique_ptr<FillRectOp> Make(std::optional<Color> color, const Rect& rect,
                                          const Matrix& viewMatrix,
                                          const Matrix& localMatrix = Matrix::I());

  bool add(std::optional<Color> color, const Rect& rect, const Matrix& viewMatrix,
           const Matrix& localMatrix);

  void prepare(Context* context) override;

  void execute(RenderPass* renderPass) override;

 private:
  DEFINE_OP_CLASS_ID

  FillRectOp(std::optional<Color> color, const Rect& rect, const Matrix& viewMatrix,
             const Matrix& localMatrix);

  bool onCombineIfPossible(Op* op) override;

  bool canAdd(size_t count) const;

  std::shared_ptr<Data> getVertexData();

  std::vector<float> coverageVertices() const;

  std::vector<float> noCoverageVertices() const;

  bool needsIndexBuffer() const;

  std::vector<Color> colors;
  std::vector<Rect> rects;
  std::vector<Matrix> viewMatrices;
  std::vector<Matrix> localMatrices;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy;
  std::shared_ptr<GpuBufferProxy> indexBufferProxy;
};
}  // namespace tgfx
