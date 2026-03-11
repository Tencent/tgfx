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

#pragma once

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#include <QQuickItem>
#include <QSGTexture>
#pragma clang diagnostic pop

#include "tgfx/gpu/Drawable.h"

namespace tgfx {
class RenderTargetProxy;

/**
 * QGLDrawable represents a Qt OpenGL rendering target backed by a framework-managed texture.
 * It provides a RenderTargetProxy for rendering and exposes the underlying texture as a QSGTexture
 * for display by the Qt Scene Graph. After calling present(), use getQSGTexture() to retrieve the
 * texture for the most recently presented frame.
 */
class QGLDrawable : public Drawable {
 public:
  QGLDrawable(QQuickItem* quickItem, int width, int height,
              std::shared_ptr<ColorSpace> colorSpace = nullptr);
  ~QGLDrawable() override;

  /**
   * Returns a QSGTexture wrapping this drawable's GL texture for display by the Qt Scene Graph.
   * Returns nullptr if the texture has not yet been created (i.e., present() has not been called).
   * The returned QSGTexture is owned by this QGLDrawable and is invalidated on the next call.
   */
  QSGTexture* getQSGTexture();

 protected:
  bool isReusable() const override {
    return false;
  }
  std::shared_ptr<RenderTargetProxy> getProxy(Context* context) override;
  void onPresent(Context* context) override;

 private:
  QQuickItem* quickItem = nullptr;
  std::shared_ptr<RenderTargetProxy> proxy = nullptr;
  unsigned textureID = 0;
  QSGTexture* outTexture = nullptr;
};
}  // namespace tgfx
