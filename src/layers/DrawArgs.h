/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <tgfx/core/Recorder.h>
#include <tgfx/core/Surface.h>
#include "tgfx/gpu/Context.h"

namespace tgfx {

enum class DrawMode { Normal, Contour, Background };

class BackgroundArgs {
 public:
  BackgroundArgs(Context* context, const Rect& renderRect, float scale)
      : renderRect(renderRect), scale(scale) {
    this->renderRect.roundOut();
    init(context);
  }

  Matrix invertMatrix() const {
    auto matrix = Matrix::MakeScale( 1 / scale, 1 / scale);
    matrix.preTranslate(renderRect.x(), renderRect.y());
    return matrix;
  }

  Canvas* canvas() {
    return _canvas;
  }

  std::shared_ptr<Image> getBackgroundImage(const Rect&) const {
    // auto rect = backgroundRect;
    // rect.intersect(renderRect);
    // rect.offset(-renderRect.x(), -renderRect.y());
    // rect.scale(scale, scale);
    auto image = surface->makeImageSnapshot();
    // return image->makeSubset(rect);
    return image;
  }

  void init(Context* context) {
    auto rect = renderRect;
    rect.scale(scale, scale);
    rect.roundOut();
    surface =
        Surface::Make(context, static_cast<int>(rect.width()), static_cast<int>(rect.height()));
    _canvas = surface->getCanvas();
    _canvas->clear();
    _canvas->scale(scale, scale);
    _canvas->translate(-renderRect.x(), -renderRect.y());
  }
  Canvas* _canvas = nullptr;
  std::shared_ptr<Surface> surface;
  Rect renderRect;
  float scale = 1.0f;
};

/**
 * DrawArgs represents the arguments passed to the draw method of a Layer.
 */
class DrawArgs {
 public:
  DrawArgs() = default;

  DrawArgs(Context* context, bool excludeEffects = false, DrawMode drawMode = DrawMode::Normal)
      : context(context), excludeEffects(excludeEffects), drawMode(drawMode) {
  }

  // The GPU context to be used during the drawing process. Note: this could be nullptr.
  Context* context = nullptr;
  // Whether to exclude effects during the drawing process.
  bool excludeEffects = false;
  // Determines the draw mode of the Layer.
  DrawMode drawMode = DrawMode::Normal;
  // The rectangle area to be drawn. This is used for clipping the drawing area.
  Rect* renderRect = nullptr;

  // The background args.
  BackgroundArgs* backgroundArgs = nullptr;
};
}  // namespace tgfx
