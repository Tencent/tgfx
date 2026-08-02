/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <memory>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {
class Context;
class RenderTargetProxy;

class AOTPlanExecutor {
 public:
  /** Returns whether the current production executor can preserve this plan's semantics. */
  static bool CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Builds an atomic render task for a supported linear AOT plan without enqueueing it. On failure,
   * originalDraw remains unchanged. On success, ownership of originalDraw moves into the task.
   */
  static PlacementPtr<RenderTask> Make(Context* context, uint32_t renderFlags,
                                       const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                       const Rect& deviceBounds,
                                       std::shared_ptr<RenderTargetProxy> destination,
                                       PlacementPtr<DrawOp>* originalDraw);
};
}  // namespace tgfx
