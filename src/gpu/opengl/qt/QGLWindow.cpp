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
#include "core/utils/Log.h"
#include "tgfx/gpu/opengl/qt/QGLDrawable.h"

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
  if (colorSpace != nullptr && !NearlyEqual(currentColorSpace.get(), colorSpace.get())) {
    LOGE(
        "QGLWindow::MakeFrom() The specified ColorSpace does not match the window's ColorSpace. "
        "Rendering may have color inaccuracies.");
  }
  auto window =
      std::shared_ptr<QGLWindow>(new QGLWindow(quickItem, singleBufferMode, std::move(colorSpace)));
  window->weakThis = window;
  window->initDevice();
  return window;
}

QGLWindow::QGLWindow(QQuickItem* quickItem, bool singleBufferMode,
                     std::shared_ptr<ColorSpace> colorSpace)
    : quickItem(quickItem), singleBufferMode(singleBufferMode), colorSpace(std::move(colorSpace)) {
  if (QThread::currentThread() != QApplication::instance()->thread()) {
    renderThread = QThread::currentThread();
  }
}

QGLWindow::~QGLWindow() {
  delete deviceCreator;
}

void QGLWindow::moveToThread(QThread* thread) {
  std::lock_guard<std::mutex> autoLock(locker);
  renderThread = thread;
  if (device != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
}

std::shared_ptr<Drawable> QGLWindow::onCreateDrawable(Context*) {
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
  auto maxCount = singleBufferMode ? 1 : 2;
  for (auto& drawable : drawables) {
    if (drawable->_available && drawable->width() == width && drawable->height() == height) {
      drawable->_available = false;
      return drawable;
    }
  }
  if (static_cast<int>(drawables.size()) >= maxCount) {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  auto drawable = std::make_shared<QGLDrawable>(quickItem, width, height, colorSpace);
  drawable->_available = false;
  drawables.push_back(drawable);
  return drawable;
}

void QGLWindow::onInvalidSize() {
  drawables.clear();
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
