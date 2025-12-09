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

#include "tgfx/gpu/opengl/qt/QGLWindow.h"
#include <QApplication>
#include <QColorSpace>
#include <QQuickWindow>
#include <QThread>
#include "core/utils/ColorSpaceHelper.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
class QGLDeviceCreator : public QObject {
  Q_OBJECT
 public:
  QGLDeviceCreator(QQuickItem* item, QGLWindow* window) : item(item), window(window) {
    auto nativeWindow = item->window();
    if (nativeWindow != nullptr) {
      handleWindowChanged(nativeWindow);
    } else {
      connect(item, SIGNAL(windowChanged(QQuickWindow*)), this,
              SLOT(handleWindowChanged(QQuickWindow*)));
    }
  }

  ~QGLDeviceCreator() override {
    disconnect(item, SIGNAL(windowChanged(QQuickWindow*)), this,
               SLOT(handleWindowChanged(QQuickWindow*)));
    auto nativeWindow = item->window();
    if (nativeWindow != nullptr) {
      disconnect(nativeWindow, SIGNAL(beforeRendering()), this, SLOT(onBeforeRendering()));
    }
  }

 private:
  QQuickItem* item = nullptr;
  QGLWindow* window = nullptr;

  Q_SLOT
  void handleWindowChanged(QQuickWindow* nativeWindow) {
    disconnect(item, SIGNAL(windowChanged(QQuickWindow*)), this,
               SLOT(handleWindowChanged(QQuickWindow*)));
    if (nativeWindow != nullptr) {
      auto shareContext = getShareContext();
      if (shareContext && shareContext->thread() == QThread::currentThread()) {
        // We are on the same thread as the QSG render thread, so we can create the device here.
        createDevice(shareContext);
      } else {
        connect(nativeWindow, SIGNAL(beforeRendering()), this, SLOT(onBeforeRendering()),
                Qt::DirectConnection);
        QMetaObject::invokeMethod(item, "update", Qt::AutoConnection);
      }
    }
  }

  Q_SLOT
  void onBeforeRendering() {
    disconnect(item->window(), SIGNAL(beforeRendering()), this, SLOT(onBeforeRendering()));
    auto shareContext = getShareContext();
    createDevice(shareContext);
  }

  QOpenGLContext* getShareContext() {
    auto nativeWindow = item->window();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    return reinterpret_cast<QOpenGLContext*>(nativeWindow->rendererInterface()->getResource(
        nativeWindow, QSGRendererInterface::OpenGLContextResource));
#else
    return nativeWindow->openglContext();
#endif
  }

  void createDevice(QOpenGLContext* shareContext) {
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
        app, [self = window->weakThis.lock(), context] { self->createDevice(context); },
        Qt::AutoConnection);
  }
};

std::shared_ptr<QGLWindow> QGLWindow::MakeFrom(QQuickItem* quickItem, bool singleBufferMode,
                                               std::shared_ptr<ColorSpace> colorSpace) {
  if (quickItem == nullptr) {
    return nullptr;
  }
  auto nativeWindow = quickItem->window();
  auto icc = nativeWindow->format().colorSpace().iccProfile();
  std::shared_ptr<ColorSpace> currentColorSpace =
      ColorSpace::MakeFromICC(icc.data(), static_cast<size_t>(icc.size()));
  if (colorSpace != nullptr &&
      !ColorSpace::NearlyEquals(currentColorSpace.get(), colorSpace.get())) {
    LOGE(
        "The ColorSpace is not adapt with the current window colorSpace, which may cause color "
        "inaccuracies on Window.");
  }
  auto window =
      std::shared_ptr<QGLWindow>(new QGLWindow(quickItem, singleBufferMode, std::move(colorSpace)));
  window->weakThis = window;
  window->initDevice();
  return window;
}

QGLWindow::QGLWindow(QQuickItem* quickItem, bool singleBufferMode,
                     std::shared_ptr<ColorSpace> colorSpace)
    : quickItem(quickItem), singleBufferMode(singleBufferMode) {
  if (QThread::currentThread() != QApplication::instance()->thread()) {
    renderThread = QThread::currentThread();
  }
  this->colorSpace = std::move(colorSpace);
}

QGLWindow::~QGLWindow() {
  delete outTexture;
  delete deviceCreator;
}

void QGLWindow::moveToThread(QThread* thread) {
  std::lock_guard<std::mutex> autoLock(locker);
  renderThread = thread;
  if (device != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
}

QSGTexture* QGLWindow::getQSGTexture() {
  std::lock_guard<std::mutex> autoLock(locker);
  auto nativeWindow = quickItem->window();
  if (nativeWindow == nullptr || device == nullptr) {
    return nullptr;
  }
  if (pendingTextureID > 0 && pendingSurface != nullptr) {
    if (outTexture) {
      delete outTexture;
      outTexture = nullptr;
    }
    auto width = static_cast<int>(ceil(quickItem->width()));
    auto height = static_cast<int>(ceil(quickItem->height()));
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    outTexture = QNativeInterface::QSGOpenGLTexture::fromNative(
        pendingTextureID, nativeWindow, QSize(width, height), QQuickWindow::TextureHasAlphaChannel);

#elif (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    outTexture = nativeWindow->createTextureFromNativeObject(
        QQuickWindow::NativeObjectTexture, &pendingTextureID, 0, QSize(width, height),
        QQuickWindow::TextureHasAlphaChannel);
#else
    outTexture = nativeWindow->createTextureFromId(pendingTextureID, QSize(width, height),
                                                   QQuickWindow::TextureHasAlphaChannel);
#endif
    // Keep the surface alive until the next getQSGTexture() call.
    displayingSurface = pendingSurface;
    pendingSurface = nullptr;
    pendingTextureID = 0;
    if (!singleBufferMode) {
      std::swap(frontSurface, surface);
    }
  }
  return outTexture;
}

std::shared_ptr<Surface> QGLWindow::onCreateSurface(Context* context) {
  auto nativeWindow = quickItem->window();
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  auto pixelRatio = nativeWindow->devicePixelRatio();
  auto width = static_cast<int>(ceil(quickItem->width() * pixelRatio));
  auto height = static_cast<int>(ceil(quickItem->height() * pixelRatio));
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (!singleBufferMode) {
    frontSurface =
        Surface::Make(context, width, height, ColorType::RGBA_8888, 1, false, 0, colorSpace);
    if (frontSurface == nullptr) {
      return nullptr;
    }
  }
  auto backSurface =
      Surface::Make(context, width, height, ColorType::RGBA_8888, 1, false, 0, colorSpace);
  if (backSurface == nullptr) {
    frontSurface = nullptr;
    return nullptr;
  }
  sizeInvalid = false;
  return backSurface;
}

void QGLWindow::onPresent(Context*) {
  GLTextureInfo textureInfo = {};
  // Surface->getBackendTexture() triggers a flush, so we need to call it under the context.
  surface->getBackendTexture().getGLTextureInfo(&textureInfo);
  pendingTextureID = textureInfo.id;
  // Keep the surface alive until the next getQSGTexture() call.
  pendingSurface = surface;
  QMetaObject::invokeMethod(quickItem, "update", Qt::AutoConnection);
}

void QGLWindow::onFreeSurface() {
  frontSurface = nullptr;
  surface = nullptr;
}

void QGLWindow::initDevice() {
  deviceCreator = new QGLDeviceCreator(quickItem, this);
}

void QGLWindow::createDevice(QOpenGLContext* context) {
  std::lock_guard<std::mutex> autoLock(locker);
  auto surface = new QOffscreenSurface();
  surface->setFormat(context->format());
  surface->create();
  device = QGLDevice::MakeFrom(context, surface, true);
  if (renderThread != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
  QMetaObject::invokeMethod(quickItem, "update", Qt::AutoConnection);
}
}  // namespace tgfx

#include "QGLWindow.moc"