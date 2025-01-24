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

#include "OpContext.h"
#include "gpu/DrawingManager.h"
#include "gpu/ops/RectDrawOp.h"

namespace tgfx {
void OpContext::fillWithFP(std::unique_ptr<FragmentProcessor> fp, const Matrix& uvMatrix,
                           bool autoResolve) {
  fillRectWithFP(bounds(), std::move(fp), uvMatrix, autoResolve);
}

void OpContext::fillRectWithFP(const Rect& dstRect, std::unique_ptr<FragmentProcessor> fp,
                               const Matrix& uvMatrix, bool autoResolve) {
  if (fp == nullptr) {
    return;
  }
  auto op = RectDrawOp::Make(std::nullopt, dstRect, Matrix::I(), uvMatrix);
  op->addColorFP(std::move(fp));
  op->setBlendMode(BlendMode::Src);
  addOp(std::move(op));
  if (autoResolve) {
    auto drawingManager = renderTarget->getContext()->drawingManager();
    drawingManager->addTextureResolveTask(renderTarget);
  }
}

void OpContext::addOp(std::unique_ptr<Op> op) {
  if (opsTask == nullptr || opsTask->isClosed()) {
    auto drawingManager = renderTarget->getContext()->drawingManager();
    opsTask = drawingManager->addOpsTask(renderTarget, renderFlags);
  }
  opsTask->addOp(std::move(op));
}

void OpContext::replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTarget) {
  DEBUG_ASSERT(newRenderTarget != nullptr);
  opsTask = nullptr;
  renderTarget = std::move(newRenderTarget);
}

}  // namespace tgfx
