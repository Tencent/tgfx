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
/**
 * QGLDrawable is a Drawable for rendering within the Qt Scene Graph. After rendering, use
 * getQSGTexture() to retrieve the texture for the most recently presented frame.
 */
class QGLDrawable : public Drawable {
 public:
  /**
   * Creates a new QGLDrawable for rendering within the Qt Scene Graph.
   * @param quickItem the QQuickItem associated with this drawable.
   * @param width the width of the drawable surface in pixels.
   * @param height the height of the drawable surface in pixels.
   * @param colorSpace optional color space for rendering. If nullptr, the default sRGB is used.
   */
  QGLDrawable(QQuickItem* quickItem, int width, int height,
              std::shared_ptr<ColorSpace> colorSpace = nullptr);
  ~QGLDrawable() override;

  /**
   * Returns a QSGTexture for display by the Qt Scene Graph. Returns nullptr if the drawable has not
   * been presented yet. The returned QSGTexture is owned by this QGLDrawable.
   */
  QSGTexture* getQSGTexture();

 protected:
  bool isReusable() const override {
    return false;
  }
  void onPresent(Context* context, std::shared_ptr<CommandBuffer> commandBuffer) override;

 private:
  QQuickItem* quickItem = nullptr;
  unsigned textureID = 0;
  QSGTexture* outTexture = nullptr;

  std::shared_ptr<DrawableProxy> onCreateProxy(Context* context) override;
};
}  // namespace tgfx
