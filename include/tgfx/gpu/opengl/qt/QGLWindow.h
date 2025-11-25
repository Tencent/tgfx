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
#include <QQuickItem>
#include <QSGTexture>
#pragma clang diagnostic pop
#include "QGLDevice.h"
#include "tgfx/gpu/Window.h"

namespace tgfx {
class QGLDeviceCreator;

class QGLWindow : public Window {
 public:
  ~QGLWindow() override;

  /**
   * Creates a new QGLWindow from specified QQuickItem and shared context. This method can be called
   * from any thread. And the returned QGLWindow is safe to be used or destructed on other threads
   * after calling moveToThread(). If the drawing process is only performed within the
   * updatePaintNode() method, you can set singleBufferMode to true to reduce the memory usage.
   * However, if you intend to perform drawing in other threads, you must set singleBufferMode to
   * false.
   */
  static std::shared_ptr<QGLWindow> MakeFrom(QQuickItem* quickItem, bool singleBufferMode = false,
                                             std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Changes the thread affinity for this object and its children.
   */
  void moveToThread(QThread* renderThread);

  /**
   * Returns the current QSGTexture for displaying. This method can only be called from the QSG
   * render thread.
   */
  QSGTexture* getQSGTexture();

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onPresent(Context* context) override;
  void onFreeSurface() override;

 private:
  std::weak_ptr<QGLWindow> weakThis;
  QQuickItem* quickItem = nullptr;
  bool singleBufferMode = false;
  QThread* renderThread = nullptr;
  unsigned pendingTextureID = 0;
  std::shared_ptr<Surface> pendingSurface = nullptr;
  std::shared_ptr<Surface> displayingSurface = nullptr;
  std::shared_ptr<Surface> frontSurface = nullptr;
  QSGTexture* outTexture = nullptr;
  QGLDeviceCreator* deviceCreator = nullptr;

  explicit QGLWindow(QQuickItem* quickItem, bool singleBufferMode = false,
                     std::shared_ptr<ColorSpace> colorSpace = nullptr);
  void initDevice();
  void createDevice(QOpenGLContext* context);

  friend class QGLDeviceCreator;
};
}  // namespace tgfx
