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
#include "gpu/aot/DrawIR.h"

namespace tgfx {
DrawOp::DrawOp(BlockAllocator* allocator, AAType aaType) : allocator(allocator), aaType(aaType) {
}

DrawOp::~DrawOp() = default;

void DrawOp::moveCoveragesToColors() {
  for (auto& coverage : coverages) {
    colors.emplace_back(std::move(coverage));
  }
  coverages.clear();
  ir = nullptr;
}

void DrawOp::tryLowerIR() {
  if (type() == Type::AtlasTextOp) {
    // The atlas glyph leaf is synthesized from the AtlasText geometry processor, which the op
    // only makes at prepare time (it needs the render target). Such draws keep the plain path
    // until the geometry contract can carry the glyph mask at record time.
    return;
  }
  std::vector<const FragmentProcessor*> colorList = {};
  colorList.reserve(colors.size());
  for (const auto& processor : colors) {
    colorList.push_back(processor.get());
  }
  std::vector<const FragmentProcessor*> coverageList = {};
  coverageList.reserve(coverages.size());
  for (const auto& processor : coverages) {
    coverageList.push_back(processor.get());
  }
  // No geometry processor exists at record time, so the IR carries no glyph leaf — matching the
  // null-GP contract of LowerDraw. A lowering failure leaves the IR null, and the plain FP path
  // serves the draw exactly as it did before the IR existed.
  auto lowered =
      LowerDraw(colorList, coverageList, nullptr, xferProcessor.get(), blendMode, allocator);
  if (!lowered.has_value()) {
    return;
  }
  ir = allocator->make<DrawIR>(std::move(*lowered));
}

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
