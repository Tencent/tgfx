/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "RenderContext.h"
#include "gpu/DrawingManager.h"
#include "gpu/ops/FillRectOp.h"

namespace tgfx {
void RenderContext::addOp(std::unique_ptr<Op> op, bool resolveNow) {
  if (opsTask == nullptr || opsTask->isClosed()) {
    auto drawingManager = renderTargetProxy->getContext()->drawingManager();
    opsTask = drawingManager->addOpsTask(renderTargetProxy);
  }
  opsTask->addOp(std::move(op));
  if (resolveNow) {
    auto drawingManager = renderTargetProxy->getContext()->drawingManager();
    drawingManager->addTextureResolveTask(renderTargetProxy);
  }
}

void RenderContext::fillWithFP(std::unique_ptr<FragmentProcessor> fp, const Matrix& localMatrix,
                               bool autoResolve) {
  fillRectWithFP(Rect::MakeWH(renderTargetProxy->width(), renderTargetProxy->height()),
                 std::move(fp), localMatrix);
  if (autoResolve) {
    auto drawingManager = renderTargetProxy->getContext()->drawingManager();
    drawingManager->addTextureResolveTask(renderTargetProxy);
  }
}

void RenderContext::fillRectWithFP(const Rect& dstRect, std::unique_ptr<FragmentProcessor> fp,
                                   const Matrix& localMatrix) {
  if (fp == nullptr) {
    return;
  }
  auto op = FillRectOp::Make(std::nullopt, dstRect, Matrix::I(), &localMatrix);
  op->addColorFP(std::move(fp));
  op->setBlendMode(BlendMode::Src);
  addOp(std::move(op));
}
}  // namespace tgfx
