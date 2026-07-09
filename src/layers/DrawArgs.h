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

  // When true, every Layer.allowsEdgeAntialiasing read site treats the layer as if edge AA is
  // disabled, so paint.antiAlias / clip antiAlias end up false and OpsCompositor::getAAType
  // returns AAType::None. Set to true by DisplayList::drawRootLayer when entering the SSAA tile
  // path; propagates automatically along the DrawArgs copy chain (DrawArgs childArgs = parentArgs)
  // into all derived intermediate surfaces — layer style offscreen (OffscreenRenderer),
  // background snapshot (BackgroundCapturer), 3D leaf surfaces (Render3DContext), etc.
  bool forceNoEdgeAA = false;

  // [SSAA-DBG] When true, every image sampling site inside a Layer draw path overrides its
  // default sampling to nearest-neighbor. Rationale: SSAA renders to a 2x tile that is later
  // Linear-downsampled to the atlas, so the SSAA downsample itself supplies the box-averaging
  // needed for anti-aliasing. Doing another 4-tap Linear sample when *entering* the SSAA tile
  // (subtree-cache hit blit, offscreen-filter result blit, etc.) burns fragment work with no
  // visible quality gain. Set to true by DisplayList::drawRootLayer only when the SSAA path
  // is active; propagates along the DrawArgs copy chain. Non-SSAA callers keep the historical
  // Linear behavior (their single-pass raster has no downstream box-averaging to lean on).
  bool forceImageSamplingNearest = false;

  // Extra scale factor propagated from the SSAA tile path. When > 1.0, subtree caches built
  // during this pass are sized at physical (canvas-matrix) resolution rather than logical
  // resolution, so the cached image carries the same sub-pixel information the SSAA path
  // would have produced by supersampling. The value is also encoded into the subtree cache
  // key, so SSAA-on and SSAA-off frames use separate cache entries (both may co-exist only
  // if the caller keeps both modes alive; DisplayList::setUseSSAA drops the tile atlas which
  // is the common trigger for the previous mode's images to become purgeable).
  // Non-SSAA path leaves this at 1.0f.
  float subtreeContentScaleDivisor = 1.0f;
};
}  // namespace tgfx
