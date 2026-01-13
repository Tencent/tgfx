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

#include <memory>
#include <stack>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class BackgroundContext;
class Context;

struct Context3DState {
  Context3DState(const Matrix3D& transform, bool antialiasing)
      : transform(transform), antialiasing(antialiasing) {
  }

  Matrix3D transform = {};
  bool antialiasing = true;
  PictureRecorder recorder = {};
};

/**
 * Base class for 3D context rendering, handling recording, transformation accumulation,
 * and picture-to-image conversion.
 */
class Base3DContext {
 public:
  static std::shared_ptr<Base3DContext> Make(bool contourMode, Context* context,
                                             const Rect& renderRect, const Point& offset,
                                             float contentScale,
                                             std::shared_ptr<ColorSpace> colorSpace,
                                             std::shared_ptr<BackgroundContext> backgroundContext);

  Base3DContext(float contentScale, std::shared_ptr<ColorSpace> colorSpace)
      : _contentScale(contentScale), _colorSpace(std::move(colorSpace)) {
  }

  virtual ~Base3DContext() = default;

  /**
   * Begins recording a new layer with the specified transform and antialiasing setting.
   * @param childTransform The 3D transform to apply to the layer content.
   * @param antialiasing Whether to enable edge antialiasing for this layer.
   * @return A canvas to draw the layer content on.
   */
  Canvas* beginRecording(const Matrix3D& childTransform, bool antialiasing);

  /**
   * Ends recording the current layer.
   */
  void endRecording();

  /**
   * Returns true if all layers have been recorded and the context is ready to finish.
   */
  bool isFinished() const {
    return _stateStack.empty();
  }

  /**
   * Finishes the 3D rendering and draws the result to the target canvas.
   * @param canvas The target canvas to draw the composited result on.
   * @param antialiasing Whether to enable antialiasing when drawing.
   */
  virtual void finishAndDrawTo(Canvas* canvas, bool antialiasing) = 0;

 protected:
  virtual void onBeginRecording(Canvas* canvas, const Matrix3D& transform) = 0;
  virtual void onEndRecording(std::shared_ptr<Image> image, const Matrix3D& transform,
                              const Point& pictureOffset, bool antialiasing) = 0;

  float _contentScale = 1.0f;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::stack<Context3DState> _stateStack = {};
};

}  // namespace tgfx
