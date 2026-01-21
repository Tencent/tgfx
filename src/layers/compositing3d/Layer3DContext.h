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
#include "tgfx/core/Picture.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class BackgroundContext;
class Context;
class ContourContext;

struct TransformState {
  TransformState(const Matrix3D& transform, bool antialiasing)
      : transform(transform), antialiasing(antialiasing) {
  }

  Matrix3D transform = {};
  bool antialiasing = true;
};

/**
 * Abstract base class for 3D context rendering. Handles recording, transformation
 * accumulation, and compositing of layer content with perspective effects.
 */
class Layer3DContext {
 public:
  static std::shared_ptr<Layer3DContext> Make(bool contourMode, Context* context,
                                              const Rect& renderRect, float contentScale,
                                              std::shared_ptr<ColorSpace> colorSpace,
                                              std::shared_ptr<BackgroundContext> backgroundContext);

  Layer3DContext(const Rect& renderRect, float contentScale,
                 std::shared_ptr<ColorSpace> colorSpace);
  virtual ~Layer3DContext() = default;

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
  bool isFinished() const;

  /**
   * Returns the current ContourContext for contour rendering, or nullptr for normal rendering.
   * Must be called after beginRecording.
   */
  virtual ContourContext* currentContourContext() {
    return nullptr;
  }

  /**
   * Finishes the 3D rendering and draws the result to the target canvas.
   * @param canvas The target canvas to draw the composited result on.
   * @param antialiasing Whether to enable antialiasing when drawing.
   */
  virtual void finishAndDrawTo(Canvas* canvas, bool antialiasing) = 0;

 protected:
  static std::shared_ptr<Image> PictureToImage(std::shared_ptr<Picture> picture, Point* offset,
                                               std::shared_ptr<ColorSpace> colorSpace);

  Matrix3D currentTransform() const;

  virtual Canvas* onBeginRecording() = 0;
  virtual std::shared_ptr<Picture> onFinishRecording() = 0;
  virtual void onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                            const Point& pictureOffset, int depth, bool antialiasing) = 0;

  Rect _renderRect = {};
  float _contentScale = 1.0f;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::stack<TransformState> _transformStack = {};
};

}  // namespace tgfx
