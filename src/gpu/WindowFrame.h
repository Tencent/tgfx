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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

class RenderTargetProxy;

/**
 * The automatic presentation frame of Surface::MakeFrom(context, window). Each flush cycle of a
 * window surface collects one WindowFrame per window into the drawing buffer's frame list, so
 * the automatic path shares the single collection/delivery pipeline with explicit drawables:
 * the frame registers its presentation on the batch at construction (instead of through
 * Context::present()), and the submission pipeline schedules and delivers it through the render
 * targets' proxies (which present without needing the window on frame-level backends). The
 * frame holds its window weakly: presenting must not keep the window (and through it the
 * device) alive, so a flushed buffer never forms a device reference cycle.
 */
class WindowFrame : public Drawable {
 public:
  /**
   * Creates an automatic frame for the given window and render targets. The render targets are
   * the stable proxies created by Window::onCreateRenderTarget() and carry the presentation.
   */
  static std::shared_ptr<WindowFrame> Make(std::shared_ptr<Window> window,
                                           std::shared_ptr<RenderTargetProxy> renderTarget);

  ~WindowFrame() override;

  /**
   * Appends another render target of the same window to this frame.
   */
  void addTarget(std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns true when the given render target is already part of this frame.
   */
  bool hasTarget(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

  /**
   * Returns the render target that carries this frame's presentation.
   */
  std::shared_ptr<RenderTargetProxy> primaryTarget() const;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;

 private:
  WindowFrame(std::shared_ptr<Window> window, std::shared_ptr<RenderTargetProxy> renderTarget);

  std::vector<std::shared_ptr<RenderTargetProxy>> renderTargets = {};
};

}  // namespace tgfx
