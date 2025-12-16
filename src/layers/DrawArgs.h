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

#include <unordered_set>
#include "Render3DContext.h"
#include "layers/BackgroundContext.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

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

  uint32_t renderFlags = 0;

  // Whether to exclude effects during the drawing process. When set to true, all layer styles and
  // filters will be skipped, and styleSourceTypes will be ignored.
  bool excludeEffects = false;
  // Specifies which layer style types to draw based on their extra source type. This field is only
  // effective when excludeEffects is false.
  std::unordered_set<LayerStyleExtraSourceType> styleSourceTypes = {
      LayerStyleExtraSourceType::None, LayerStyleExtraSourceType::Contour,
      LayerStyleExtraSourceType::Background};
  // Determines the draw mode of the Layer.
  DrawMode drawMode = DrawMode::Normal;
  // The rectangle area to be drawn. This is used for clipping the drawing area.
  Rect* renderRect = nullptr;

  // The background context to be used during the drawing process. Note: this could be nullptr.
  std::shared_ptr<BackgroundContext> blurBackground = nullptr;
  // Indicates whether to force drawing the background, even if there are no background styles.
  bool forceDrawBackground = false;
  std::shared_ptr<ColorSpace> dstColorSpace = ColorSpace::SRGB();

  // The maximum cache size (single edge) for subtree layer caching. Set to 0 to disable
  // subtree layer cache.
  int subtreeCacheMaxSize = 0;

  // The 3D render context to be used during the drawing process. Note: this could be nullptr. All
  // layers within the 3D rendering context need to maintain their respective 3D states to achieve
  // per-pixel depth occlusion effects. These layers are composited through the Compositor and do
  // not need to be drawn to the Canvas.
  std::shared_ptr<Render3DContext> render3DContext = nullptr;

  // Indicates whether to clip the content by the canvas.
  bool clipContentByCanvas = false;
};
}  // namespace tgfx
