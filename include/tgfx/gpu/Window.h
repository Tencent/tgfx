/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
/**
 * Window represents a native displayable resource that can be rendered or written to by a Device.
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
   * Returns the Surface associated with this Window. If the queryOnly is true, it will not create
   * a new surface if it doesn't exist.
   */
  std::shared_ptr<tgfx::Surface> getSurface(Context* context, bool queryOnly = false);

  /**
   * Applies all pending graphics changes to the window. On the Android platform, the
   * presentationTime will be passed to the SurfaceTexture.getTimestamp() method, while it will be
   * ignored on other platforms. The time is measured in microseconds. If the presentationTime is
   * not set, a system timestamp will be used instead.
   */
  void present(Context* context, int64_t presentationTime = INT64_MIN);

  /**
   * Invalidates the cached surface associated with this Window. This is useful when the window is
   * resized and the surface needs to be recreated.
   */
  void invalidSize();

  /**
   * Frees the cached surface associated with this Window immediately. This is useful when the
   * window is hidden and the surface is no longer needed for a while.
   */
  void freeSurface();

 protected:
  std::mutex locker = {};
  bool sizeInvalid = false;
  std::shared_ptr<Device> device = nullptr;
  std::shared_ptr<Surface> surface = nullptr;

  explicit Window(std::shared_ptr<Device> device);
  Window() = default;

  virtual std::shared_ptr<Surface> onCreateSurface(Context* context) = 0;
  virtual void onPresent(Context* context, int64_t presentationTime) = 0;
  virtual void onFreeSurface();

 private:
  bool checkContext(Context* context);
};
}  // namespace tgfx
