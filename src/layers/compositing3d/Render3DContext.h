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

#include <stack>
#include "Layer3DContext.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

class BackgroundContext;
class Context3DCompositor;

/**
 * Manages the rendering state for layers in a 3D context, handling recording, transformation
 * accumulation, and compositing of layer content with perspective effects.
 */
class Render3DContext : public Layer3DContext {
 public:
  Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Rect& renderRect,
                  float contentScale, std::shared_ptr<ColorSpace> colorSpace,
                  std::shared_ptr<BackgroundContext> backgroundContext);

  void finishAndDrawTo(Canvas* canvas, bool antialiasing) override;

 protected:
  Canvas* onBeginRecording() override;
  std::shared_ptr<Picture> onFinishRecording() override;
  void onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                    const Point& pictureOffset, bool antialiasing) override;

 private:
  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  std::shared_ptr<BackgroundContext> _backgroundContext = nullptr;
  std::stack<PictureRecorder> _recorderStack = {};
};

}  // namespace tgfx
