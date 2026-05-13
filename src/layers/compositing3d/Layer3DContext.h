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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class Context;
class Layer;
class DrawArgs;

/**
 * Abstract base class for 3D context rendering. The contract is two-phase:
 *  - Build phase: drawByStarting3DContext calls addLayer(rootLayer, ...). The context recursively
 *    expands the preserve3D subtree internally and accumulates per-node geometry. No pixel work
 *    happens during build.
 *  - Composite phase: finishAndDrawTo() rasterizes each registered node by invoking the standard
 *    Layer::drawLayer / Layer::drawContour entry points and composites the results onto the
 *    target canvas. preserve3D middle nodes are kept apart from their descendants because
 *    Layer::drawChildren detects args.render3DContext != nullptr and stops descending — the
 *    descendants are independently registered nodes.
 */
class Layer3DContext : public std::enable_shared_from_this<Layer3DContext> {
 public:
  /**
   * Creates a Layer3DContext implementation for compositing layer content with 3D transforms.
   * @param opaqueMode If true, returns an Opaque3DContext that renders contour content;
   * if false, returns a Render3DContext implementation.
   * @param context The GPU Context used for offscreen compositing.
   * @param renderRect The destination rectangle in the target canvas's coordinate space.
   * @param contentScale The scale factor applied to recorded content.
   * @param colorSpace The color space used for intermediate images.
   */
  static std::shared_ptr<Layer3DContext> Make(bool opaqueMode, Context* context,
                                              const Rect& renderRect, float contentScale,
                                              std::shared_ptr<ColorSpace> colorSpace);

  Layer3DContext(const Rect& renderRect, float contentScale,
                 std::shared_ptr<ColorSpace> colorSpace);
  virtual ~Layer3DContext() = default;

  /**
   * Type of pointer-to-member function used to rasterize a registered node. Mirrors
   * Layer::LayerDrawFunc — typically &Layer::drawLayer or &Layer::drawContour.
   */
  using LayerDrawFunc = bool (Layer::*)(const DrawArgs&, Canvas*, float, BlendMode);

  /**
   * Build phase: registers the root of a 3D subtree. The context internally walks the subtree
   * (recursing through preserve3D descendants and stopping at non-preserve3D leaves) and records
   * a node descriptor for each layer that should participate in 3D compositing. No raster work
   * happens here.
   * @param layer The 3D subtree root.
   * @param transform Accumulated 3D transform from outside the subtree down to this layer.
   * @param alpha Accumulated alpha along the path from outside the subtree.
   * @param drawFunc The Layer member function used to rasterize each registered node. Typically
   * &Layer::drawLayer for normal compositing or &Layer::drawContour when the 3D subtree is being
   * recorded as a contour source.
   */
  virtual void addLayer(Layer* layer, const Matrix3D& transform, float alpha,
                        LayerDrawFunc drawFunc) = 0;

  /**
   * Composite phase: rasterizes registered nodes and draws the composited result onto canvas.
   * Each node is rasterized by invoking Layer::drawLayer (or Layer::drawContour for opaque mode)
   * with args.render3DContext pointing at this context, so drawChildren can detect the 3D
   * raster context and avoid double-drawing descendants that are themselves registered nodes.
   */
  virtual void finishAndDrawTo(const DrawArgs& args, Canvas* canvas) = 0;

 protected:
  Rect _renderRect = {};
  float _contentScale = 1.0f;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

}  // namespace tgfx
