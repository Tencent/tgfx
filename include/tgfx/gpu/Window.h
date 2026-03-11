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

#include <mutex>
#include "tgfx/gpu/Drawable.h"

namespace tgfx {
class Device;

/**
 * Window represents a native displayable resource that can be rendered to by a Device. It serves
 * as a factory for creating Drawable objects that provide the actual rendering targets. The caller
 * uses getDrawable() to obtain a Drawable, then creates a Surface from it for rendering.
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
   * Returns a Drawable for rendering to this window. Returns the cached drawable if it is reusable,
   * otherwise creates a new one. If queryOnly is true, returns the last drawable without creating a
   * new one.
   */
  std::shared_ptr<Drawable> getDrawable(Context* context, bool queryOnly = false);

  /**
   * Marks the window size as invalid. The next call to getDrawable() will query the current window
   * size and create a new appropriately-sized Drawable.
   */
  void invalidSize();

 protected:
  std::mutex locker = {};
  std::shared_ptr<Device> device = nullptr;
  std::shared_ptr<Drawable> currentDrawable = nullptr;

  explicit Window(std::shared_ptr<Device> device);
  Window() = default;

  /**
   * Creates a new Drawable for this window. Subclasses implement platform-specific Drawable
   * creation. Called by getDrawable() when a new Drawable is needed.
   */
  virtual std::shared_ptr<Drawable> onCreateDrawable(Context* context) = 0;

  /**
   * Called when the window size is invalidated. Subclasses can override this to clean up
   * size-dependent resources (e.g., cached EGL surfaces). The default implementation does nothing.
   */
  virtual void onInvalidSize();
};
}  // namespace tgfx
