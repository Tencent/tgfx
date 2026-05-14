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
#include "QGLDrawableProxy.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "gpu/proxies/RenderTargetProxy.h"

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
    : quickItem(quickItem), maxTextureCount(singleBufferMode ? 1 : 2) {
  _colorSpace = std::move(colorSpace);
  if (QThread::currentThread() != QApplication::instance()->thread()) {
    renderThread = QThread::currentThread();
  }
}

QGLWindow::~QGLWindow() {
  delete deviceCreator;
  drawableProxy = nullptr;
  textureSlots.clear();
  delete presentedQSGTexture;
}

void QGLWindow::moveToThread(QThread* thread) {
  std::lock_guard<std::mutex> autoLock(locker);
  renderThread = thread;
  if (device != nullptr) {
    static_cast<QGLDevice*>(device.get())->moveToThread(renderThread);
  }
}

QSGTexture* QGLWindow::getQSGTexture() {
  std::shared_ptr<RenderTargetProxy> localProxy = nullptr;
  {
    std::lock_guard<std::mutex> autoLock(locker);
    localProxy = std::move(pendingProxy);
  }
  if (localProxy != nullptr) {
    auto textureView = localProxy->getTextureView();
    GLTextureInfo info = {};
    if (textureView != nullptr && textureView->getBackendTexture().getGLTextureInfo(&info)) {
      auto nativeWindow = quickItem->window();
      if (nativeWindow != nullptr) {
        delete presentedQSGTexture;
        presentedQSGTexture = nullptr;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        presentedQSGTexture = QNativeInterface::QSGOpenGLTexture::fromNative(
            info.id, nativeWindow, QSize(localProxy->width(), localProxy->height()),
            QQuickWindow::TextureHasAlphaChannel);
#elif (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
        presentedQSGTexture = nativeWindow->createTextureFromNativeObject(
            QQuickWindow::NativeObjectTexture, &info.id, 0,
            QSize(localProxy->width(), localProxy->height()), QQuickWindow::TextureHasAlphaChannel);
#else
        presentedQSGTexture = nativeWindow->createTextureFromId(
            info.id, QSize(localProxy->width(), localProxy->height()),
            QQuickWindow::TextureHasAlphaChannel);
#endif
      }
    }
    reuseTexture(localProxy);
  }
  return presentedQSGTexture;
}

std::shared_ptr<RenderTargetProxy> QGLWindow::onCreateRenderTarget(Context* context) {
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
  drawableProxy = std::make_shared<QGLDrawableProxy>(context, width, height, PixelFormat::RGBA_8888,
                                                     1, ImageOrigin::BottomLeft, this);
  std::static_pointer_cast<QGLDrawableProxy>(drawableProxy)->weakThis = drawableProxy;
  return drawableProxy;
}

void QGLWindow::onPresent(Context*) {
  if (presentingProxy == nullptr) {
    return;
  }
  auto proxy = std::static_pointer_cast<QGLDrawableProxy>(presentingProxy);
  presentingProxy = nullptr;
  if (proxy->getTextureView() == nullptr) {
    proxy->releaseTexture();
    return;
  }
  std::shared_ptr<RenderTargetProxy> oldProxy = nullptr;
  {
    std::lock_guard<std::mutex> autoLock(locker);
    oldProxy = std::move(pendingProxy);
    pendingProxy = proxy->getTextureTargetProxy();
    proxy->textureRTProxy = nullptr;
  }
  // Release the proxy from the previous pending frame if it was not consumed.
  if (oldProxy != nullptr) {
    reuseTexture(oldProxy);
  }
  QMetaObject::invokeMethod(quickItem, "update", Qt::AutoConnection);
}

std::shared_ptr<RenderTargetProxy> QGLWindow::acquireTexture(Context* context, int width,
                                                             int height) {
  std::lock_guard<std::mutex> autoLock(locker);
  // In single buffer mode, reclaim the pending frame if it hasn't been consumed yet.
  // This handles the case where draw() is called before getQSGTexture() in singleBufferMode.
  // In dual buffer mode, we have 2 slots available, so no need to reclaim the pending proxy.
  if (maxTextureCount == 1 && pendingProxy != nullptr) {
    for (auto& slot : textureSlots) {
      if (slot.proxy == pendingProxy) {
        slot.available = true;
        break;
      }
    }
    pendingProxy = nullptr;
  }
  // Find an available slot with matching size, and remove stale slots with mismatched size.
  auto it = textureSlots.begin();
  while (it != textureSlots.end()) {
    if (!it->available) {
      ++it;
      continue;
    }
    if (it->proxy->width() == width && it->proxy->height() == height) {
      it->available = false;
      return it->proxy;
    }
    it = textureSlots.erase(it);
  }
  if (static_cast<int>(textureSlots.size()) >= maxTextureCount) {
    LOGE("QGLWindow::acquireTexture() All textures are in use. No available texture in the pool.");
    return nullptr;
  }
  auto proxy = RenderTargetProxy::Make(context, width, height, false);
  if (proxy == nullptr) {
    return nullptr;
  }
  textureSlots.push_back({proxy, false});
  return proxy;
}

void QGLWindow::reuseTexture(const std::shared_ptr<RenderTargetProxy>& proxy) {
  std::lock_guard<std::mutex> autoLock(locker);
  for (auto& slot : textureSlots) {
    if (slot.proxy == proxy) {
      slot.available = true;
      return;
    }
  }
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
