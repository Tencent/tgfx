/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "Render3DContext.h"
#include "layers/BackgroundContext.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

enum class DrawMode { Normal, Contour, Background };

/**
 * DrawArgs represents the arguments passed to the draw method of a Layer.
 */
class DrawArgs {
 public:
  DrawArgs() = default;

  DrawArgs(Context* context, bool excludeEffects = false, DrawMode drawMode = DrawMode::Normal,
           std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB())
      : context(context), excludeEffects(excludeEffects), drawMode(drawMode),
        dstColorSpace(std::move(colorSpace)) {
  }

  // The GPU context to be used during the drawing process. Note: this could be nullptr.
  Context* context = nullptr;
  // Whether to exclude effects during the drawing process.
  bool excludeEffects = false;
  // Determines the draw mode of the Layer.
  DrawMode drawMode = DrawMode::Normal;
  // The rectangle area to be drawn. This is used for clipping the drawing area.
  Rect* renderRect = nullptr;

  // The background context to be used during the drawing process. Note: this could be nullptr.
  std::shared_ptr<BackgroundContext> blurBackground = nullptr;
  // Indicates whether to force drawing the background, even if there are no background styles.
  bool forceDrawBackground = false;
  std::shared_ptr<ColorSpace> dstColorSpace = ColorSpace::SRGB();

  // Only used while recording layer's background image.
  std::shared_ptr<BackgroundContext> blendModeBackground = nullptr;

  // The 3D render context to be used during the drawing process. Note: this could be nullptr. All
  // layers within the 3D rendering context need to maintain their respective 3D states to achieve
  // per-pixel depth occlusion effects. These layers are composited through the Compositor and do
  // not need to be drawn to the Canvas.
  std::shared_ptr<Render3DContext> render3DContext = nullptr;
};
}  // namespace tgfx
