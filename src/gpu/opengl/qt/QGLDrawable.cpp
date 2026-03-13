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

#include "tgfx/gpu/opengl/qt/QGLDrawable.h"
#include <QMetaObject>
#include <QQuickWindow>
#include "QGLDrawableProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/opengl/GLTypes.h"

namespace tgfx {
QGLDrawable::QGLDrawable(QQuickItem* quickItem, int width, int height,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(width, height, std::move(colorSpace)), quickItem(quickItem) {
}

QGLDrawable::~QGLDrawable() {
  delete outTexture;
}

QSGTexture* QGLDrawable::getQSGTexture() {
  if (textureID == 0) {
    return nullptr;
  }
  auto nativeWindow = quickItem->window();
  if (nativeWindow == nullptr) {
    return nullptr;
  }
  if (outTexture) {
    delete outTexture;
    outTexture = nullptr;
  }
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  outTexture = QNativeInterface::QSGOpenGLTexture::fromNative(
      textureID, nativeWindow, QSize(width(), height()), QQuickWindow::TextureHasAlphaChannel);
#elif (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
  outTexture = nativeWindow->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture,
                                                           &textureID, 0, QSize(width(), height()),
                                                           QQuickWindow::TextureHasAlphaChannel);
#else
  outTexture = nativeWindow->createTextureFromId(textureID, QSize(width(), height()),
                                                 QQuickWindow::TextureHasAlphaChannel);
#endif
  return outTexture;
}

std::shared_ptr<RenderTargetProxy> QGLDrawable::onCreateProxy(Context* context) {
  return std::make_shared<QGLDrawableProxy>(context, width(), height(), onGetPixelFormat(),
                                            onGetSampleCount(), onGetOrigin());
}

void QGLDrawable::onPresent(Context*, std::shared_ptr<CommandBuffer>) {
  if (textureID == 0 && _proxy != nullptr) {
    auto textureView = _proxy->getTextureView();
    if (textureView != nullptr) {
      GLTextureInfo info = {};
      if (textureView->getBackendTexture().getGLTextureInfo(&info)) {
        textureID = info.id;
      }
    }
  }
  QMetaObject::invokeMethod(quickItem, "update", Qt::AutoConnection);
}
}  // namespace tgfx
