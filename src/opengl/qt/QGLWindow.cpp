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

#include "tgfx/opengl/qt/QGLWindow.h"
#include <QApplication>
#include <QQuickWindow>
#include <QThread>
#include "gpu/Texture.h"
#include "opengl/GLRenderTarget.h"
#include "opengl/GLSampler.h"
#include "tgfx/opengl/GLFunctions.h"

namespace tgfx {

std::shared_ptr<QGLWindow> QGLWindow::MakeFrom(QQuickItem* quickItem,
                                               QOpenGLContext* sharedContext) {
  if (quickItem == nullptr) {
    return nullptr;
  }
  auto device = QGLDevice::Make(sharedContext);
  if (device == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<QGLWindow>(new QGLWindow(device, quickItem));
}

QGLWindow::QGLWindow(std::shared_ptr<Device> device, QQuickItem* quickItem)
    : DoubleBufferedWindow(std::move(device)), quickItem(quickItem) {
}

QGLWindow::~QGLWindow() {
  delete outTexture;
}

void QGLWindow::moveToThread(QThread* targetThread) {
  std::lock_guard<std::mutex> autoLock(locker);
  static_cast<QGLDevice*>(device.get())->moveToThread(targetThread);
}

QSGTexture* QGLWindow::getTexture() {
  std::lock_guard<std::mutex> autoLock(locker);
  auto nativeWindow = quickItem->window();
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  if (textureInvalid && frontSurface && backSurface) {
    textureInvalid = false;
    if (outTexture) {
      delete outTexture;
      outTexture = nullptr;
    }
    GLTextureInfo frontTextureInfo;
    frontSurface->getBackendTexture().getGLTextureInfo(&frontTextureInfo);
    auto textureID = frontTextureInfo.id;
    auto width = static_cast<int>(ceil(quickItem->width()));
    auto height = static_cast<int>(ceil(quickItem->height()));
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    outTexture = QNativeInterface::QSGOpenGLTexture::fromNative(
        textureID, nativeWindow, QSize(width, height), QQuickWindow::TextureHasAlphaChannel);

#elif (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    outTexture = nativeWindow->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                             &textureID, 0, QSize(width, height),
                                                             QQuickWindow::TextureHasAlphaChannel);
#else
    outTexture = nativeWindow->createTextureFromId(textureID, QSize(width, height),
                                                   QQuickWindow::TextureHasAlphaChannel);
#endif
  }
  return outTexture;
}

void QGLWindow::invalidateTexture() {
  std::lock_guard<std::mutex> autoLock(locker);
  textureInvalid = true;
}

std::shared_ptr<Surface> QGLWindow::onCreateSurface(Context* context) {
  auto nativeWindow = quickItem->window();
  auto pixelRatio = nativeWindow ? nativeWindow->devicePixelRatio() : 1.0f;
  auto width = static_cast<int>(ceil(quickItem->width() * pixelRatio));
  auto height = static_cast<int>(ceil(quickItem->height() * pixelRatio));
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  frontSurface = Surface::MakeFrom(Texture::MakeRGBA(context, width, height));
  backSurface = Surface::MakeFrom(Texture::MakeRGBA(context, width, height));
  if (frontSurface == nullptr || backSurface == nullptr) {
    return nullptr;
  }
  return backSurface;
}

void QGLWindow::onSwapSurfaces(Context*) {
  invalidateTexture();
}
}  // namespace tgfx
