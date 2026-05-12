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

#include "tgfx/gpu/CommandEncoder.h"

namespace tgfx {
/**
 * A two-pass test helper used to verify that the GPU backend honors stencil writes and stencil
 * compares. The first pass uses a scissor rect to write a constant value into the stencil buffer
 * on the left half of the attachment (compare = Always, passOp = Replace); the right half stays at
 * the cleared value 0. The second pass clears the color attachment to black and then draws red
 * over the entire viewport, gated by the stencil compare = Equal. If the backend correctly applies
 * stencil state in both passes, the resulting color attachment is red on the left half and black
 * on the right half; if either side of the stencil pipeline (write or compare) is silently
 * skipped, both halves are either red or black, which the test catches.
 */
class StencilWriteReadPass {
 public:
  static std::shared_ptr<StencilWriteReadPass> Make();

  bool onDraw(CommandEncoder* encoder, std::shared_ptr<Texture> renderTexture) const;

 private:
  StencilWriteReadPass() = default;

  std::shared_ptr<RenderPipeline> createPipeline(GPU* gpu, bool stencilWrite) const;
};
}  // namespace tgfx
