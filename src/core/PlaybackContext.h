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

#include "core/ClipStack.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class DrawContext;

class PlaybackContext {
 public:
  PlaybackContext() = default;

  PlaybackContext(const Matrix& matrix, const ClipStack& clip);

  const Matrix& matrix() const {
    return _matrix;
  }

  const ClipStack& clip() const {
    return _clip;
  }

  const Brush& brush() const {
    return _brush;
  }

  const Stroke* stroke() const {
    return hasStroke ? &_stroke : nullptr;
  }

  void setMatrix(const Matrix& matrix);

  void setClip(const ClipStack& clip);

  void setColor(const Color& color);

  void setBrush(const Brush& brush);

  void setStrokeWidth(float width);

  void setStroke(const Stroke& stroke);

  void setHasStroke(bool value);

  void drawFill(DrawContext* context);

 private:
  Matrix initMatrix = {};
  ClipStack initClip = {};
  bool hasInitMatrix = false;
  bool hasInitClip = false;
  Matrix _matrix = {};
  ClipStack _clip = {};
  Brush _brush = {};
  Stroke _stroke = {};
  bool hasStroke = false;
};
}  // namespace tgfx
