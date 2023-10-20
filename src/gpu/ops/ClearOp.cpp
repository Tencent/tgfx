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

#include "ClearOp.h"
#include "gpu/RenderPass.h"

namespace tgfx {
std::unique_ptr<ClearOp> ClearOp::Make(Color color, const Rect& scissor) {
  return std::unique_ptr<ClearOp>(new ClearOp(color, scissor));
}

bool ContainsScissor(const Rect& a, const Rect& b) {
  return a.isEmpty() || (!b.isEmpty() && a.contains(b));
}

bool ClearOp::onCombineIfPossible(Op* op) {
  auto that = static_cast<ClearOp*>(op);
  if (ContainsScissor(that->scissor, scissor)) {
    scissor = that->scissor;
    color = that->color;
    return true;
  } else if (color == that->color && ContainsScissor(scissor, that->scissor)) {
    return true;
  }
  return false;
}

void ClearOp::execute(RenderPass* renderPass) {
  renderPass->clear(scissor, color);
}
}  // namespace tgfx
