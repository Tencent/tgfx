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
 * A two-pass test helper used to verify that the GPU backend honors stencil write operations even
 * when the stencil compare function is Always. The first pass writes a constant value into the
 * stencil buffer using compare = Always, passOp = Replace. The second pass clears the color
 * attachment to black and then draws red only where the stencil equals the reference value. If the
 * backend correctly applies the stencil state during the first pass, the resulting color
 * attachment is fully red; otherwise it remains black.
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
