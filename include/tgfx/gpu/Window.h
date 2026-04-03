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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <mutex>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
class RenderTargetProxy;

/**
 * Window represents a native displayable resource that can be rendered to by a Device. Use
 * Surface::MakeFrom(context, window) to obtain a Surface for rendering, then call
 * context->submit() to automatically present the result.
 */
class Window {
 public:
  virtual ~Window() = default;

  /**
   * Returns the Device associated with this Window. It may return null if the window is still in
   * the process of initializing.
   */
  std::shared_ptr<Device> getDevice();

  /**
   * Returns the color space associated with this Window. Returns nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const;

 protected:
  std::mutex locker = {};
  std::shared_ptr<Device> device = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  int sampleCount = 1;

  explicit Window(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace = nullptr,
                  int sampleCount = 1);
  Window() = default;

  /**
   * Creates a platform-specific RenderTargetProxy for this window. Subclasses must implement this
   * to provide the appropriate render target for the platform's graphics API. Returns nullptr if
   * the render target cannot be created.
   */
  virtual std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) = 0;

  /**
   * Called after command buffer submission to present the rendered content. The default
   * implementation does nothing.
   */
  virtual void onPresent(Context* context);

 private:
  friend class DrawingBuffer;
  friend class Surface;
};
}  // namespace tgfx
