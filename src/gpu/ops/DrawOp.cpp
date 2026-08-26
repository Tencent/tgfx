/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "DrawOp.h"
#include <algorithm>
#include "core/utils/MathExtra.h"
#include "gpu/OriginFlip.h"

namespace tgfx {
void DrawOp::applyScissor(RenderPass* renderPass, RenderTarget* renderTarget) const {
  if (scissorRect.isEmpty()) {
    renderPass->setScissorRect(0, 0, renderTarget->width(), renderTarget->height());
    return;
  }
  // scissorRect lives in canvas top-left device space. Flip it into the backend's scissor
  // origin space just before handing off — GL windows use BottomLeft and expect the flipped
  // rect, other backends (Metal/Vulkan/WebGPU) keep TopLeft and the helper is a no-op.
  auto flipped = scissorRect;
  FlipYIfNeeded(&flipped, renderTarget);
  // roundOut + clamp to rt extent: partial-pixel truncation drops geometry; out-of-bounds
  // scissors are silently rejected by some drivers.
  int scissorX = std::max(0, FloatFloorToInt(flipped.left));
  int scissorY = std::max(0, FloatFloorToInt(flipped.top));
  int scissorRight = std::min(renderTarget->width(), FloatCeilToInt(flipped.right));
  int scissorBottom = std::min(renderTarget->height(), FloatCeilToInt(flipped.bottom));
  int scissorWidth = std::max(0, scissorRight - scissorX);
  int scissorHeight = std::max(0, scissorBottom - scissorY);
  renderPass->setScissorRect(scissorX, scissorY, scissorWidth, scissorHeight);
}
}  // namespace tgfx
