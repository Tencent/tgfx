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
#include "gpu/GpuBuffer.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class TriangulatingPathOp : public DrawOp {
 public:
  DEFINE_OP_CLASS_ID

  static std::unique_ptr<TriangulatingPathOp> Make(Color color, const Path& path,
                                                   const Matrix& viewMatrix,
                                                   const Stroke* stroke = nullptr,
                                                   uint32_t renderFlags = 0);

  ~TriangulatingPathOp() override;

  void prepare(Context* context) override;

  void execute(RenderPass* renderPass) override;

 protected:
  bool onCombineIfPossible(Op* op) override;

 private:
  Color color = Color::Transparent();
  Path path = {};
  Matrix viewMatrix = Matrix::I();
  Matrix rasterizeMatrix = Matrix::I();
  Stroke* stroke = nullptr;
  uint32_t renderFlags = 0;
  std::shared_ptr<GpuBufferProxy> vertexBuffer = nullptr;

  TriangulatingPathOp(Color color, Path path, const Matrix& viewMatrix, const Stroke* stroke,
                      uint32_t renderFlags);
};
}  // namespace tgfx
