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

#include <optional>
#include "EGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class EGLWindow : public Window {
 public:
  /**
   * Returns an EGLWindow associated with current EGLSurface. Returns nullptr if there is no current
   * EGLSurface on the calling thread.
   */
  static std::shared_ptr<EGLWindow> Current();

  /**
   * Creates a new window from an EGL native window with specified shared context.
   */
  static std::shared_ptr<EGLWindow> MakeFrom(EGLNativeWindowType nativeWindow,
                                             EGLContext sharedContext = nullptr);

  /**
   * Sets the presentation time for the next frame in microseconds. This is only applicable on
   * Android. The presentation time will be forwarded to the SurfaceTexture.getTimestamp() method.
   * If not specified, a system timestamp will be used by default.
   */
  void setPresentationTime(int64_t presentationTime);

 protected:
  void onInvalidSize() override;
  std::shared_ptr<Surface> onCreateSurface(Context* context,
                                           std::shared_ptr<ColorSpace> colorSpace) override;
  void onPresent(Context* context) override;

 private:
  EGLNativeWindowType nativeWindow;
  std::optional<int64_t> presentationTime = std::nullopt;

  explicit EGLWindow(std::shared_ptr<Device> device);
};
}  // namespace tgfx
