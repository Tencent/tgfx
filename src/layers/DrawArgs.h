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

#include <memory>
#include <vector>
#include "layers/compositing3d/Layer3DContext.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class BackgroundHandler;
class OpaqueContext;

/**
 * DrawArgs represents the arguments passed to the draw method of a Layer.
 */
class DrawArgs {
 public:
  DrawArgs() = default;

  DrawArgs(Context* context, bool excludeEffects = false,
           std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB())
      : context(context), excludeEffects(excludeEffects), dstColorSpace(std::move(colorSpace)) {
  }

  // The GPU context to be used during the drawing process. Note: this could be nullptr.
  Context* context = nullptr;

  uint32_t renderFlags = 0;

  // Whether to exclude effects during the drawing process.
  // Note: When set to true, all layer styles and filters will be skipped.
  bool excludeEffects = false;
  // World-space cull rects for this draw pass. When non-null and non-empty, drawLayer culls each
  // Layer whose renderBounds does not intersect any of these rects. Callers should pre-outset the
  // rects if blur sampling range needs to be included. nullptr means no culling (full recording).
  const std::vector<Rect>* renderRects = nullptr;

  std::shared_ptr<ColorSpace> dstColorSpace = ColorSpace::SRGB();

  // The maximum cache size (single edge) for subtree layer caching. Set to 0 to disable
  // subtree layer cache.
  int subtreeCacheMaxSize = 0;

  // The 3D render context to be used during the drawing process.
  // Note: this could be nullptr. All layers within the 3D rendering context need to maintain their
  // respective 3D states to achieve per-pixel depth occlusion effects. These layers are composited
  // through the Compositor and do not need to be drawn to the Canvas.
  std::shared_ptr<Layer3DContext> render3DContext = nullptr;

  // The opaque context to be used during opaque content/contour recording. Note: this could be nullptr.
  OpaqueContext* opaqueContext = nullptr;

  // Active background handler: BackgroundCapturer / BackgroundConsumer / NoOp(). Set to
  // BackgroundHandler::NoOp() on intermediate-artifact paths (mask prep, contour, subtree cache,
  // layer style source, 3D entry). nullptr falls back to NoOp in DispatchOrSkip.
  BackgroundHandler* backgroundHandler = nullptr;
};
}  // namespace tgfx
