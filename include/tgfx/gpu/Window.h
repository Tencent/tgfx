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
#include <vector>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Device.h"

namespace tgfx {
class Drawable;
class RenderTargetProxy;

/**
 * Window represents a native displayable resource that can be rendered to by a Device. Use
 * Surface::MakeFrom(context, window) to obtain a Surface for rendering, then call
 * context->submit() to automatically present the result. To read back the rendered content, use
 * nextDrawable() to acquire a Drawable instead, which keeps the frame buffer readable and allows
 * manual presentation.
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
   * Acquires the next single-frame Drawable for rendering with manual presentation and readback.
   * The returned Drawable is used to create a Surface via Surface::MakeFrom(context, drawable).
   * The rendering commands must be submitted before calling Drawable::readPixels() or
   * Drawable::present(); the frame is presented either manually via Drawable::present() or
   * automatically when the Drawable is released. The Drawable retains this Window until the frame
   * is released. Only one Drawable should be held at a time, otherwise frame acquisition may stall.
   * Returns nullptr if the context is nullptr or belongs to a different Device, the window is not
   * owned by a shared_ptr, or the window cannot provide a drawable right now (for example, the
   * window has a zero size or the platform frame buffer is unavailable).
   */
  std::shared_ptr<Drawable> nextDrawable(Context* context);

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
  std::shared_ptr<Device> device = nullptr;
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
   * Called after command buffer submission to present a group of render targets with the same
   * presentation identity. Backends with a shared native backbuffer receive all render targets for
   * the Window in one call. Backends with independent frame buffers receive one render target per
   * call. The default implementation does nothing.
   * @param context The Context that submitted the rendering commands.
   * @param renderTargets The render targets that produced the frame being presented.
   */
  virtual void onPresent(Context* context,
                         const std::vector<std::shared_ptr<RenderTargetProxy>>& renderTargets);

  /**
   * Returns true if every RenderTargetProxy represents an independently presentable frame. The
   * default is false for backends where all proxies target one shared native backbuffer.
   */
  virtual bool hasIndependentPresentationTargets() const;

  /**
   * Creates a backend-specific Drawable for nextDrawable(). The default implementation wraps this
   * window's onCreateRenderTarget() and onPresent(), which works for backends that present inside
   * onPresent(). Backends that schedule the presentation when the drawable is acquired (Metal,
   * Vulkan) override this to defer the presentation to Drawable::present(). The default
   * implementation returns nullptr if the window cannot provide a render target right now.
   */
  virtual std::shared_ptr<Drawable> onNextDrawable(Context* context);

 private:
  friend class DrawingBuffer;
  friend class DrawingManager;
  friend class Surface;
  friend class WindowDrawable;
};
}  // namespace tgfx
