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
#include "tgfx/core/Surface.h"

namespace tgfx {

/**
 * The internal Surface subclass behind Surface::MakeFrom(context, window): the automatic,
 * cross-frame-reusable window entry. It holds the window and collects a WindowFrame for each
 * drawing batch (which registers its presentation at collection and is delivered after the
 * batch is submitted); the next batch starts a fresh frame. Created only through
 * Surface::MakeFrom(); business code always sees a plain Surface.
 */
class WindowSurface : public Surface {
 public:
  static std::shared_ptr<Surface> Make(Context* context, std::shared_ptr<Window> window,
                                       uint32_t renderFlags);

 protected:
  void onCollectFrame(DrawingManager* drawingManager,
                      const std::shared_ptr<RenderTargetProxy>& renderTarget) override;

  bool onValidateReadback() const override;

 private:
  WindowSurface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags, bool clearAll,
                std::shared_ptr<ColorSpace> colorSpace, std::shared_ptr<Window> window);

  std::shared_ptr<Window> _window = nullptr;
};

}  // namespace tgfx
