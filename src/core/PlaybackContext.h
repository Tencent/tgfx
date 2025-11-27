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

#include "core/MCState.h"
#include "tgfx/core/BrushModifier.h"

namespace tgfx {

class PlaybackContext {
 public:
  PlaybackContext() = default;

  explicit PlaybackContext(MCState state, const BrushModifier* brushModifier = nullptr);

  const MCState& state() const {
    return _state;
  }

  const Brush& brush() const {
    return _brush;
  }

  const Stroke* stroke() const {
    return hasStroke ? &_stroke : nullptr;
  }

  void setMatrix(const Matrix& matrix);

  void setClip(const Path& clip);

  void setColor(const Color& color);

  void setBrush(const Brush& brush);

  void setStrokeWidth(float width);

  void setStroke(const Stroke& stroke);

  void setHasStroke(bool value);

  void drawFill(DrawContext* context);

 private:
  MCState initState = {};
  bool hasInitMatrix = false;
  bool hasInitClip = false;
  const BrushModifier* brushModifier = nullptr;
  MCState _state = {};
  Brush _brush = {};
  Brush lastOriginalBrush = {};
  Stroke _stroke = {};
  bool hasStroke = false;
};
}  // namespace tgfx
