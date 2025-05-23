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

#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
class RootLayer;

/**
 * DisplayList represents a collection of layers can be drawn to a Surface. Note: All layers in the
 * display list are not thread-safe and should only be accessed from a single thread.
 */
class DisplayList {
 public:
  DisplayList();

  virtual ~DisplayList() = default;

  /**
   * Returns the root layer of the display list. Note: The root layer cannot be added to another
   * layer. Therefore, properties like alpha, blendMode, position, matrix, visibility, scrollRect,
   * and mask have no effect on the root layer since it will never have a parent.
   */
  Layer* root() const;

  /**
   * Returns the current scale factor applied to the layer tree. This factor determines how much the
   * layer tree is scaled during rendering. Adjusting the zoomScale to scale the layer tree is more
   * efficient than applying a matrix directly, as it avoids invalidating the layer tree's internal
   * caches. The default value is 1.0f.
   */
  float zoomScale() const {
    return _zoomScale;
  }

  /**
   * Sets the scale factor for the layer tree. This factor determines how much the layer tree is
   * scaled during rendering. Adjusting the zoomScale to scale the layer tree is more efficient than
   * applying a matrix directly, as it avoids invalidating the layer tree's internal caches. The
   * default value is 1.0f.
   */
  void setZoomScale(float zoomScale);

  /**
   * Returns the current content offset of the layer tree after applying the zoomScale. This offset
   * determines how far the origin of the layer tree is shifted relative to the surface's origin.
   * Adjusting the contentOffset to move the layer tree is more efficient than applying a matrix
   * directly, as it avoids invalidating the layer tree's internal caches. The default value is
   * (0, 0).
   */
  const Point& contentOffset() const {
    return _contentOffset;
  }

  /**
   * Sets the content offset of the layer tree after applying the zoomScale. This offset determines
   * how far the origin of the layer tree is shifted relative to the surface's origin. Adjusting the
   * contentOffset to move the layer tree is more efficient than applying a matrix directly, as it
   * avoids invalidating the layer tree's internal caches. The default value is (0, 0).
   */
  void setContentOffset(float offsetX, float offsetY);

  /**
   * Returns true if the content of the display list has changed since the last rendering. This can
   * be used to determine if the display list needs to be re-rendered.
   */
  bool hasContentChanged() const;

  /**
   * Renders the display list onto the given surface.
   * @param surface The surface to render the display list on.
   * @param autoClear If true, the surface will be cleared before rendering the display list.
   * Otherwise, the display list will be rendered over the existing content.
   */
  void render(Surface* surface, bool autoClear = true);

 private:
  std::shared_ptr<RootLayer> _root = nullptr;
  float _zoomScale = 1.0f;
  Point _contentOffset = {};
  bool _hasContentChanged = false;
};
}  // namespace tgfx
