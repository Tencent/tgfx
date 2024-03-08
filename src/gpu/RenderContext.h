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

#include "gpu/processors/FragmentProcessor.h"
#include "gpu/tasks/OpsRenderTask.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
/**
 * A helper class to orchestrate commands (draws, etc...) for render targets.
 */
class RenderContext {
 public:
  explicit RenderContext(std::shared_ptr<RenderTargetProxy> renderTargetProxy)
      : renderTargetProxy(std::move(renderTargetProxy)) {
  }


  void fillWithFP(std::unique_ptr<FragmentProcessor> fp, const Matrix& localMatrix,
                  bool autoResolve = false);

  void fillRectWithFP(const Rect& dstRect, std::unique_ptr<FragmentProcessor> fp,
                      const Matrix& localMatrix);

  void addOp(std::unique_ptr<Op> op);

 private:
  std::shared_ptr<RenderTargetProxy> renderTargetProxy = nullptr;
  std::shared_ptr<OpsRenderTask> opsTask = nullptr;
};
}  // namespace tgfx
