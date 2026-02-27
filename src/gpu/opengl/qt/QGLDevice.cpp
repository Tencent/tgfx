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

#include "tgfx/gpu/opengl/qt/QGLDevice.h"
#include <QApplication>
#include <QThread>
#include "core/utils/Log.h"
#include "gpu/opengl/qt/QGLGPU.h"

namespace tgfx {
void* GLDevice::CurrentNativeHandle() {
  return QOpenGLContext::currentContext();
}

std::shared_ptr<GLDevice> GLDevice::Current() {
  auto context = QOpenGLContext::currentContext();
  auto surface = context != nullptr ? context->surface() : nullptr;
  return QGLDevice::Wrap(context, surface, true);
}

std::shared_ptr<GLDevice> GLDevice::Make(void*) {
  return QGLDevice::Make();
}

std::shared_ptr<QGLDevice> QGLDevice::Make(QOpenGLContext* sharedContext, QSurfaceFormat* format) {
  if (QThread::currentThread() != QApplication::instance()->thread()) {
    LOGE("QGLDevice::Make() can be called on the main (GUI) thread only.");
    return nullptr;
  }
  auto context = new QOpenGLContext();
  if (format) {
    context->setFormat(*format);
  }
  if (sharedContext) {
    context->setShareContext(sharedContext);
  }
  context->create();
  auto surface = new QOffscreenSurface();
  surface->setFormat(context->format());
  surface->create();
  auto device = QGLDevice::Wrap(context, surface, false);
  if (device == nullptr) {
    delete surface;
    delete context;
  }
  return device;
}

std::shared_ptr<QGLDevice> QGLDevice::MakeFrom(QOpenGLContext* context, QSurface* surface,
                                               bool adopted) {
  return QGLDevice::Wrap(context, surface, !adopted);
}

std::shared_ptr<QGLDevice> QGLDevice::Wrap(QOpenGLContext* qtContext, QSurface* qtSurface,
                                           bool externallyOwned) {
  auto glContext = GLDevice::Get(qtContext);
  if (glContext) {
    return std::static_pointer_cast<QGLDevice>(glContext);
  }
  if (qtContext == nullptr || qtSurface == nullptr) {
    return nullptr;
  }
  auto oldContext = QOpenGLContext::currentContext();
  auto oldSurface = oldContext != nullptr ? oldContext->surface() : nullptr;
  if (oldContext != qtContext) {
    if (!qtContext->makeCurrent(qtSurface)) {
      return nullptr;
    }
  }
  std::shared_ptr<QGLDevice> device = nullptr;
  auto interface = GLInterface::GetNative();
  if (interface != nullptr) {
    auto gpu = std::make_unique<QGLGPU>(std::move(interface));
    device = std::shared_ptr<QGLDevice>(new QGLDevice(std::move(gpu), qtContext));
    device->externallyOwned = externallyOwned;
    device->qtContext = qtContext;
    device->qtSurface = qtSurface;
    device->weakThis = device;
  }
  if (oldContext != qtContext) {
    qtContext->doneCurrent();
    if (oldContext != nullptr) {
      oldContext->makeCurrent(oldSurface);
    }
  }
  return device;
}

QGLDevice::QGLDevice(std::unique_ptr<GPU> gpu, void* nativeHandle)
    : GLDevice(std::move(gpu), nativeHandle) {
}

QGLDevice::~QGLDevice() {
  releaseAll();
  if (externallyOwned) {
    return;
  }
  delete qtContext;
  auto app = QApplication::instance();
  if (app != nullptr) {
    // Always delete the surface on the main thread.
    QMetaObject::invokeMethod(app, [surface = qtSurface] { delete surface; });
  } else {
    delete qtSurface;
  }
}

bool QGLDevice::sharableWith(void* nativeContext) const {
  auto shareContext = reinterpret_cast<QOpenGLContext*>(nativeContext);
  return QOpenGLContext::areSharing(shareContext, qtContext);
}

void QGLDevice::moveToThread(QThread* targetThread) {
  ownerThread = targetThread;
  qtContext->moveToThread(targetThread);
  if (qtSurface->surfaceClass() == QSurface::SurfaceClass::Offscreen) {
    static_cast<QOffscreenSurface*>(qtSurface)->moveToThread(targetThread);
  }
}

bool QGLDevice::onLockContext() {
  if (ownerThread != nullptr && QThread::currentThread() != ownerThread) {
    LOGE("QGLDevice can not be locked in a different thread.");
    return false;
  }
  oldContext = QOpenGLContext::currentContext();
  if (oldContext) {
    oldSurface = oldContext->surface();
  }
  if (oldContext == qtContext) {
    return true;
  }
  if (!qtContext->makeCurrent(qtSurface)) {
    LOGE("QGLDevice::onLockContext() failed!");
    return false;
  }
  return true;
}

void QGLDevice::onUnlockContext() {
  if (oldContext == qtContext) {
    oldContext = nullptr;
    oldSurface = nullptr;
    return;
  }
  qtContext->doneCurrent();
  if (oldContext != nullptr) {
    oldContext->makeCurrent(oldSurface);
    oldContext = nullptr;
    oldSurface = nullptr;
  }
}
}  // namespace tgfx
