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

  /**
   * Enables or disables vertical synchronization for presentation. When enabled (the default),
   * presentation is throttled to the display's refresh rate. When disabled, the calling thread is
   * no longer blocked by the display's vsync during present, which is useful when that thread must
   * not stall (for example a UI callback thread). The exact effect depends on the backend and the
   * underlying platform; backends that cannot control vsync ignore this setting. Takes effect on
   * the next present.
   *
   * Backend note: some backends (for example Vulkan and WebGPU) apply the new setting by rebuilding
   * their swap chain, which only happens when the render target is recreated. On those backends the
   * caller must recreate the Surface (via Surface::MakeFrom) after changing this setting for it to
   * take effect. Backends that switch the swap interval at present time (D3D12, WGL, EGL, CGL,
   * Metal) apply the change on the next present without recreating the Surface.
   */
  void setVSyncEnabled(bool enabled);

  /**
   * Returns whether vertical synchronization is enabled for presentation. Defaults to true.
   */
  bool vsyncEnabled();

 protected:
  std::mutex locker = {};
  std::shared_ptr<Device> device = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  bool _vsyncEnabled = true;

  explicit Window(std::shared_ptr<Device> device, std::shared_ptr<ColorSpace> colorSpace = nullptr);
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

  /**
   * Called when the vsync setting changes via setVSyncEnabled(). Subclasses that must react
   * immediately (for example by rebuilding a swap chain) override this. Invoked while holding the
   * window lock. The default implementation does nothing.
   */
  virtual void onVSyncEnabledChanged(bool enabled);

 private:
  friend class DrawingBuffer;
  friend class Surface;
};
}  // namespace tgfx
