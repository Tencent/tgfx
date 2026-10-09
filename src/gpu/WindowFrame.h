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
#include <vector>
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

class RenderTargetProxy;

/**
 * The automatic presentation frame of Surface::MakeFrom(context, window). Each flush cycle of a
 * window surface collects one WindowFrame per window into the drawing buffer's drawable list, so
 * the automatic path shares the single collection/delivery pipeline with explicit drawables:
 * the frame registers its presentation at construction (instead of Context::present()), the
 * submission pipeline schedules it onto the command buffer that carries the frame's rendering
 * (Window::onSchedulePresentation()), and delivers it after submission
 * (Window::onPresent()). Dropping the buffer or its Recording discards the frame.
 */
class WindowFrame : public Drawable {
 public:
  /**
   * Creates an automatic frame for the given window and render targets. The window is retained
   * until the frame is delivered; the render targets are the stable proxies created by
   * Window::onCreateRenderTarget().
   */
  static std::shared_ptr<WindowFrame> Make(std::shared_ptr<Window> window,
                                           std::shared_ptr<RenderTargetProxy> renderTarget);

  ~WindowFrame() override;

  /**
   * Appends another render target of the same window to this frame.
   */
  void addTarget(std::shared_ptr<RenderTargetProxy> renderTarget);

  bool isWindowFrame() const override {
    return true;
  }

  /**
   * Returns true when this frame presents the given window. Frames stop matching after delivery
   * (their window reference is released).
   */
  bool isForWindow(const std::shared_ptr<Window>& window) const;

  /**
   * Returns true when the given render target is already part of this frame.
   */
  bool hasTarget(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;

  bool onSchedulePresent(Context* context) override;

  void onPresent(Context* context) override;

 private:
  WindowFrame(std::shared_ptr<Window> window, std::shared_ptr<RenderTargetProxy> renderTarget);

  std::vector<std::shared_ptr<RenderTargetProxy>> renderTargets = {};
};

}  // namespace tgfx
