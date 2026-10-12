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

#include "gpu/WindowFrame.h"
#include <algorithm>
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {

std::shared_ptr<WindowFrame> WindowFrame::Make(std::shared_ptr<Window> window,
                                               std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (window == nullptr || renderTarget == nullptr) {
    return nullptr;
  }
  auto frame =
      std::shared_ptr<WindowFrame>(new WindowFrame(std::move(window), std::move(renderTarget)));
  // The automatic path presents every collected frame; the presentation is registered at
  // construction instead of through Context::present().
  frame->markPresentationRequested();
  return frame;
}

WindowFrame::WindowFrame(std::shared_ptr<Window> window,
                         std::shared_ptr<RenderTargetProxy> renderTarget)
    : Drawable(renderTarget->width(), renderTarget->height(), window->colorSpace()) {
  // The window is held weakly for identity only (frame aggregation); the presentation is
  // carried by the render targets' proxies.
  _window = std::move(window);
  renderTargets.push_back(std::move(renderTarget));
}

WindowFrame::~WindowFrame() {
  // Discard the frame if it was never delivered (matching the Drawable subclass contract).
  abandon();
}

void WindowFrame::addTarget(std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (renderTarget != nullptr) {
    renderTargets.push_back(std::move(renderTarget));
  }
}

bool WindowFrame::hasTarget(const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  return std::find(renderTargets.begin(), renderTargets.end(), renderTarget) != renderTargets.end();
}

std::shared_ptr<RenderTargetProxy> WindowFrame::primaryTarget() const {
  return renderTargets.empty() ? nullptr : renderTargets.front();
}

std::shared_ptr<RenderTargetProxy> WindowFrame::onImport(Context*) {
  // Automatic frames are never imported: the window surface's stable proxy provides the render
  // target directly. The frame only carries the presentation.
  return nullptr;
}

}  // namespace tgfx
