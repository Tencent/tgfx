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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QWindow>
#pragma clang diagnostic pop
#include "tgfx/gpu/opengl/GLDevice.h"

namespace tgfx {
class QGLDevice : public GLDevice {
 public:
  /**
   * Creates an offscreen QT device with specified format and shared context. If the format is not
   * specified, QSurfaceFormat::defaultFormat() will be used.
   * Note: Due to the fact that QOffscreenSurface is backed by a QWindow on some platforms,
   * cross-platform applications must ensure that this method is only called on the main (GUI)
   * thread. The returned QGLDevice is then safe to be used or destructed on other threads after
   * calling moveToThread().
   */
  static std::shared_ptr<QGLDevice> Make(QOpenGLContext* sharedContext = nullptr,
                                         QSurfaceFormat* format = nullptr);

  /**
   * Creates an QGLDevice with the existing QOpenGLContext, and QSurface. If adopted is true, the
   * QGLDevice will take ownership of the QOpenGLContext and QSurface and destroy them when the
   * QGLDevice is destroyed. The caller should not destroy the QOpenGLContext and QSurface in this
   * case.
   */
  static std::shared_ptr<QGLDevice> MakeFrom(QOpenGLContext* context, QSurface* surface,
                                             bool adopted = false);

  ~QGLDevice() override;

  bool sharableWith(void* nativeHandle) const override;

  /**
   * Returns the native OpenGL context.
   */
  QOpenGLContext* glContext() const {
    return qtContext;
  }

  /**
   * Changes the thread affinity for this object and its children.
   */
  void moveToThread(QThread* renderThread);

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  QThread* ownerThread = nullptr;
  QOpenGLContext* qtContext = nullptr;
  QSurface* qtSurface = nullptr;
  QOpenGLContext* oldContext = nullptr;
  QSurface* oldSurface = nullptr;

  static std::shared_ptr<QGLDevice> Wrap(QOpenGLContext* context, QSurface* surface,
                                         bool externallyOwned);

  QGLDevice(std::unique_ptr<GPU> gpu, void* nativeHandle);

  friend class GLDevice;
  friend class QGLWindow;
  friend class Texture;
};
}  // namespace tgfx
