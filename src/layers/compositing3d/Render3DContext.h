/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "Context3DCompositor.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

class BackgroundContext;

struct Render3DContextState {
  PictureRecorder recorder = {};
  Matrix3D transform = {};
  bool antialiasing = true;
};

/**
 * Manages the rendering state for layers in a 3D context, handling recording, transformation
 * accumulation, and compositing of layer content with perspective effects.
 */
class Render3DContext {
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
                  BackgroundContext* backgroundContext)
      : _compositor(std::move(compositor)), _offset(offset), _contentScale(contentScale),
        _colorSpace(std::move(colorSpace)), _backgroundContext(backgroundContext) {
  }

  /**
   * Returns the compositor used to combine 3D layer images.
   */
  const std::shared_ptr<Context3DCompositor>& compositor() const {
    return _compositor;
  }

  /**
   * Returns the offset of the 3D context in the target canvas coordinate space.
   */
  const Point& offset() const {
    return _offset;
  }

  /**
   * Returns the scale factor applied to the content.
   */
  float contentScale() const {
    return _contentScale;
  }

  /**
   * Returns the background context for blur effects, or nullptr if not available.
   */
  BackgroundContext* backgroundContext() const {
    return _backgroundContext;
  }

  /**
   * Begins recording a new layer with the specified transform and antialiasing setting.
   * @param childTransform The 3D transform to apply to the layer content.
   * @param antialiasing Whether to enable edge antialiasing for this layer.
   * @return A canvas to draw the layer content on.
   */
  Canvas* beginRecording(const Matrix3D& childTransform, bool antialiasing);

  /**
   * Ends recording the current layer and adds it to the compositor.
   */
  void endRecording();

 private:
  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  Point _offset = {};
  float _contentScale = 1.0f;

  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  BackgroundContext* _backgroundContext = nullptr;
  std::stack<Render3DContextState> _stateStack = {};
};

}  // namespace tgfx
