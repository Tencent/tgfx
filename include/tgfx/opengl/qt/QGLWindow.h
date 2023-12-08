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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#include <QQuickItem>
#include <QSGTexture>
#pragma clang diagnostic pop
#include "QGLDevice.h"
#include "tgfx/gpu/DoubleBufferedWindow.h"

namespace tgfx {
class Texture;
class GLRenderTarget;

class QGLWindow : public DoubleBufferedWindow {
 public:
  ~QGLWindow() override;

  /**
   * Creates a new QGLWindow from specified QQuickItem and shared context. This method can be called
   * from any thread. After creation, you can use moveToThread() to move this object to the render
   * thread you created.
   */
  static std::shared_ptr<QGLWindow> MakeFrom(QQuickItem* quickItem);

  /**
   * Changes the thread affinity for this object and its children.
   */
  void moveToThread(QThread* renderThread);

  /**
   * Returns the current QSGTexture for displaying. This method can only be called from the QSG
   * render thread.
   */
  QSGTexture* getTexture();

 protected:
  std::shared_ptr<Surface> onCreateSurface(Context* context) override;
  void onSwapSurfaces(Context*) override;

 private:
  std::mutex locker = {};
  std::weak_ptr<QGLWindow> weakThis;
  bool deviceChecked = false;
  bool textureInvalid = true;
  QThread* renderThread = nullptr;
  QQuickItem* quickItem = nullptr;
  QSGTexture* outTexture = nullptr;

  explicit QGLWindow(QQuickItem* quickItem);
  void checkDevice(QQuickWindow* window);
  void createDevice(QOpenGLContext* context);
  void invalidateTexture();
};
}  // namespace tgfx
