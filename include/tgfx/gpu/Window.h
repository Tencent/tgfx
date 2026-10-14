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
 * Surface::MakeFrom(context, window) to obtain a Surface for rendering. Every recording batch
 * that draws into the window's surfaces presents the window's frame automatically when it is
 * submitted (see Context::flush() and Context::submit()).
 */
class Window : public std::enable_shared_from_this<Window> {
 public:
  virtual ~Window() = default;

  /**
   * Returns the Device associated with this Window. It may return null if the window is still in
   * the process of initializing.
   */
  std::shared_ptr<Device> getDevice();

  /**
   * Returns true when this window's frames can be copied before presentation, that is, when
   * Surface::readPixels() and Surface::asyncReadPixels() are supported on the window's surfaces
   * (see those methods for when the content is defined). False on windows whose platform frame
   * buffer cannot act as a copy source (a CAMetalLayer with framebufferOnly set to YES, a Vulkan
   * swapchain without TRANSFER_SRC usage, or a WebGPU canvas without CopySrc usage). The default
   * implementation returns false.
   */
  virtual bool supportsReadback() const {
    return false;
  }

  /**
   * Returns the color space associated with this Window. Returns nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const;

  /**
   * Returns whether vertical synchronization is enabled for presentation. This is fixed when the
   * Window is created (defaults to true) and cannot be changed afterwards. When enabled,
   * presentation is throttled to the display's refresh rate. When disabled, the presenting thread
   * is no longer blocked by the display's vsync, which is useful when that thread must not stall
   * (for example a UI callback thread). The exact effect depends on the backend and the underlying
   * platform; backends that cannot control vsync ignore this setting.
   */
  bool vsyncEnabled() const;

 protected:
  std::mutex locker = {};
  // The device is held strongly: factory entry points like MetalWindow::MakeFrom(layer) create
  // a default device that has no other owner, and callers expect window->getDevice() to keep
  // it alive. Reference cycles with pending drawing buffers are broken on the frame side
  // instead: the window's render target proxies hold the window weakly and present through
  // their own hooks.
  std::shared_ptr<Device> _device = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  const bool _vsyncEnabled = true;

  explicit Window(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace = nullptr,
                  bool vsyncEnabled = true);
  Window() = default;

  /**
   * Creates a platform-specific RenderTargetProxy for this window. Subclasses must implement this
   * to provide the appropriate render target for the platform's graphics API. Returns nullptr if
   * the render target cannot be created.
   */
  virtual std::shared_ptr<RenderTargetProxy> onCreateRenderTarget(Context* context) = 0;

  /**
   * Presents the window's shared native backbuffer after command buffer submission. Only
   * backends where every proxy targets one shared backbuffer (GL) reach this entry point,
   * through RenderTargetProxy's default presentation forwarding; backends with frame-level
   * buffers (Metal, Vulkan, WebGPU, EAGL, QGL, D3D12) override the proxy hooks instead. The
   * default implementation does nothing.
   * @param context The Context that submitted the rendering commands.
   */
  virtual void onPresent(Context* context);

 private:
  friend class DrawingBuffer;
  friend class DrawingManager;
  friend class Surface;
  friend class RenderTargetProxy;
};
}  // namespace tgfx
