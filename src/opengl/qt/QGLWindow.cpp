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
std::shared_ptr<QGLWindow> QGLWindow::MakeFrom(QQuickItem* quickItem) {
  if (quickItem == nullptr) {
    return nullptr;
  }
  auto window = std::shared_ptr<QGLWindow>(new QGLWindow(quickItem));
  window->weakThis = window;
  return window;
}

QGLWindow::QGLWindow(QQuickItem* quickItem) : DoubleBufferedWindow(nullptr), quickItem(quickItem) {
}

QGLWindow::~QGLWindow() {
  delete outTexture;
}

void QGLWindow::moveToThread(QThread* thread) {
  std::lock_guard<std::mutex> autoLock(locker);
  renderThread = thread;
  if (device != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
}

QSGTexture* QGLWindow::getTexture() {
  std::lock_guard<std::mutex> autoLock(locker);
  auto nativeWindow = quickItem->window();
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  if (device == nullptr) {
    checkDevice(nativeWindow);
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

void QGLWindow::checkDevice(QQuickWindow* window) {
  if (deviceChecked) {
    return;
  }
  deviceChecked = true;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  auto shareContext = reinterpret_cast<QOpenGLContext*>(window->rendererInterface()->getResource(
      window, QSGRendererInterface::OpenGLContextResource));
#else
  auto shareContext = window->openglContext();
#endif
  // Creating a context that shares with a context that is current on another thread is not safe,
  // some drivers on windows can reject this. So we have to create the context here on the QSG
  // render thread.
  auto context = new QOpenGLContext();
  context->setFormat(shareContext->format());
  context->setShareContext(shareContext);
  context->create();
  auto app = QApplication::instance();
  context->moveToThread(app->thread());
  QMetaObject::invokeMethod(
      app,
      [self = weakThis.lock(), context] {
        std::lock_guard<std::mutex> autoLock(self->locker);
        self->createDevice(context);
      },
      Qt::QueuedConnection);
}

void QGLWindow::createDevice(QOpenGLContext* context) {
  auto surface = new QOffscreenSurface();
  surface->setFormat(context->format());
  surface->create();
  device = QGLDevice::MakeFrom(context, surface, true);
  if (renderThread != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
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
