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

#include <optional>
#include "tgfx/gpu/GLDrawable.h"

namespace tgfx {
/**
 * EGLDrawable represents an EGL-based rendering target backed by a default framebuffer (FBO 0).
 * It supports setting a presentation time for Android SurfaceTexture timestamps. The caller must
 * ensure that the EGLDevice (and its EGLDisplay/EGLSurface) outlives this Drawable.
 */
class EGLDrawable : public GLDrawable {
 public:
  /**
   * Creates a new EGLDrawable with the specified EGL display, surface, and dimensions. The
   * eglDisplay and eglSurface parameters accept EGLDisplay and EGLSurface handles as void* to avoid
   * pulling in EGL headers that conflict with X11 macros.
   */
  EGLDrawable(void* eglDisplay, void* eglSurface, int width, int height,
              std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Sets the presentation time for this frame in microseconds. This is only applicable on Android.
   * The time will be forwarded to eglPresentationTimeANDROID during present. If not set, a system
   * timestamp will be used by default.
   */
  void setPresentationTime(int64_t time);

 protected:
  BackendRenderTarget onCreateBackendRenderTarget() override;
  void onPresent(Context* context, std::shared_ptr<CommandBuffer> commandBuffer) override;

 private:
  void* eglDisplay = nullptr;
  void* eglSurface = nullptr;
  std::optional<int64_t> presentationTime = std::nullopt;
};
}  // namespace tgfx
