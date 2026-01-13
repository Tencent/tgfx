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

#include "Base3DContext.h"

namespace tgfx {

class BackgroundContext;
class Context3DCompositor;

/**
 * Manages the rendering state for layers in a 3D context, handling recording, transformation
 * accumulation, and compositing of layer content with perspective effects.
 */
class Render3DContext : public Base3DContext {
 public:
  /**
   * Creates a new Render3DContext.
   * @param compositor The compositor used to combine 3D layer images.
   * @param offset The offset of the 3D context in the target canvas coordinate space.
   * @param contentScale The scale factor applied to the content for higher resolution rendering.
   * @param colorSpace The color space used for intermediate images.
   * @param backgroundContext The background context for blur effects, or nullptr if not needed.
   */
  Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Point& offset,
                  float contentScale, std::shared_ptr<ColorSpace> colorSpace,
                  std::shared_ptr<BackgroundContext> backgroundContext);

  void finishAndDrawTo(Canvas* canvas, bool antialiasing) override;

 protected:
  void onBeginRecording(Canvas* canvas, const Matrix3D& transform) override;
  void onEndRecording(std::shared_ptr<Image> image, const Matrix3D& transform,
                      const Point& pictureOffset, bool antialiasing) override;

 private:
  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  Point _offset = {};
  std::shared_ptr<BackgroundContext> _backgroundContext = nullptr;
};

}  // namespace tgfx
