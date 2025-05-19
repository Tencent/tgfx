/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ResolveOp.h"
#include <InspectorDefine.h>
#include "gpu/RenderPass.h"

namespace tgfx {
PlacementPtr<ResolveOp> ResolveOp::Make(Context* context, const Rect& bounds) {
  if (bounds.isEmpty()) {
    return nullptr;
  }
  return context->drawingBuffer()->make<ResolveOp>(bounds);
}

void ResolveOp::execute(RenderPass* renderPass) {
  OperateMark(OpTaskType::ResolveOp);
  renderPass->resolve(bounds);
}

}  // namespace tgfx
