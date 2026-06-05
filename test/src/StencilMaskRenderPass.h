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
#include "tgfx/core/Color.h"
#include "tgfx/gpu/Attribute.h"
#include "tgfx/gpu/CommandEncoder.h"
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
/**
 * StencilMaskRenderPass demonstrates a two-pass stencil-based mask pipeline. It is a minimal,
 * test-only consumer that exercises the full stencil capability stack: ProgramInfo's stencil
 * descriptor and color-write-mask plumbing, RenderPipelineDescriptor::depthStencil, the GL
 * stencil-state setup and its no-op detection, and the depth/stencil texture attachment
 * lifecycle.
 *
 * Pass 1 ("mask write") draws a centred filled circle as a triangle fan with the colour write
 * mask cleared (no colour output) and the stencil configured as compare=Always +
 * passOp=Replace + ref=1, so the stencil buffer holds 1 inside the circle and 0 elsewhere.
 *
 * Pass 2 ("cover") draws a fullscreen quad with the colour write mask back to All and the
 * stencil configured as compare=Equal + ref=1 + passOp=Keep, so only fragments where the
 * stencil is 1 survive — i.e. only the masked region is shaded with the cover colour.
 *
 * Pass 1 deliberately uses `compare=Always` together with a non-Keep `passOp`, because the GL
 * stencil-state factory's old shortcut (return nullptr whenever both faces' compare is Always)
 * would silently drop this pipeline's stencil state. Hitting this case end-to-end is the
 * purpose of the test.
 */
class StencilMaskRenderPass {
 public:
  /**
   * Creates a StencilMaskRenderPass that draws a centred filled circle into the stencil buffer
   * (disc of radius 0.6 in NDC, i.e. covering ~60% of the half-width of the viewport) and
   * shades the mask with `coverColor` in pass 2.
   */
  static std::shared_ptr<StencilMaskRenderPass> Make(PMColor coverColor);

  /**
   * Encodes both stencil passes into `encoder`. `renderTexture` is the colour attachment;
   * `stencilTexture` is the DEPTH24_STENCIL8 texture that backs the stencil buffer for both
   * passes. Both passes share a single render-pass instance (and therefore the same stencil
   * buffer) — pass 1 writes, pass 2 tests against the result. Returns false on any GPU
   * resource creation failure.
   */
  bool draw(CommandEncoder* encoder, std::shared_ptr<Texture> renderTexture,
            std::shared_ptr<Texture> stencilTexture) const;

 private:
  explicit StencilMaskRenderPass(PMColor color);

  std::shared_ptr<RenderPipeline> createMaskPipeline(GPU* gpu) const;
  std::shared_ptr<RenderPipeline> createCoverPipeline(GPU* gpu) const;

  PMColor coverColor;
  Attribute position;
};
}  // namespace tgfx
