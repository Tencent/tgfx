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
class RectPaint;

class RectDrawOp : public DrawOp {
 public:
  DEFINE_OP_CLASS_ID

  static std::unique_ptr<RectDrawOp> Make(std::optional<Color> color, const Rect& rect,
                                          const Matrix& viewMatrix,
                                          const Matrix* uvMatrix = nullptr);

  void prepare(Context* context, uint32_t renderFlags) override;

  void execute(RenderPass* renderPass) override;

 private:
  RectDrawOp(std::optional<Color> color, const Rect& rect, const Matrix& viewMatrix,
             const Matrix* uvMatrix = nullptr);

  bool onCombineIfPossible(Op* op) override;

  bool canAdd(size_t count) const;

  bool needsIndexBuffer() const;

  bool hasColor = true;
  std::vector<std::shared_ptr<RectPaint>> rectPaints = {};
  std::shared_ptr<GpuBufferProxy> indexBufferProxy = nullptr;
  std::shared_ptr<GpuBufferProxy> vertexBufferProxy = nullptr;
  std::shared_ptr<Data> vertexData = nullptr;
};
}  // namespace tgfx
