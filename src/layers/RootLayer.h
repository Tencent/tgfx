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

#include <optional>
#include "tgfx/layers/Layer.h"

namespace tgfx {
// Maximum number of dirty regions that can be tracked in the root layer.
static constexpr size_t MAX_DIRTY_REGIONS = 3;

/**
 * The RootLayer class represents the root layer of a display list. It is the top-level layer that
 * contains all other layers in the display list. The root layer cannot be added to another layer.
 * Therefore, properties like alpha, blendMode, position, matrix, visibility, scrollRect, and mask
 * have no effect on the root layer since it will never have a parent.
 */
class RootLayer : public Layer {
 public:
  /**
   * Creates a new RootLayer instance.
   */
  static std::shared_ptr<RootLayer> Make();

  ~RootLayer() override;

  /**
   * Invalidates a specific rectangle in the root layer. This method is used to mark a portion of
   * the layer tree as needing to be redrawn.
   */
  void invalidateRect(const Rect& rect);

  /**
   * Iterates sourceRects, intersects each with drawRect, then expands via LayerStyle::
   * filterBackground() (or skips expansion if layerStyle is null, e.g. the hasBlendMode path)
   * and adds the expanded rects back into the root dirty list. Only rects inside sourceRects
   * are considered, so callers can pass a snapshot of dirty rects taken before descending into
   * a background-blur layer to exclude dirty rects produced by that layer's own content and
   * descendants (which paint above the blur result and are not part of the blur input).
   * Returns true if any dirty rect intersects drawRect when layerStyle is null, or if any
   * expanded rect was added back to the dirty list.
   */
  bool invalidateBackground(const Rect& drawRect, LayerStyle* layerStyle, float contentScale,
                            const std::vector<Rect>& sourceRects);

  /**
   * Returns true if there are any dirty rectangles in the root layer.
   */
  bool hasDirtyRegions() const {
    return !dirtyRects.empty();
  }

  /**
   * Returns the current dirty rect list. Used by layers that need to snapshot the list before
   * entering a background-blur subtree, or to pass the current list directly on the fast path
   * where no new rects are produced mid-traversal.
   */
  const std::vector<Rect>& currentDirtyRects() const {
    return dirtyRects;
  }

  /**
   * Resets the dirty regions of the root layer and returns the list of dirty rectangles.
   */
  std::vector<Rect> updateDirtyRegions();

  /**
   * Returns the background color of the root layer.
   */
  Color backgroundColor() const {
    return _backgroundColor;
  }

  /**
   * Sets the background color of the root layer.
   * @return true if the background color is changed.
   */
  bool setBackgroundColor(const Color& color);

 protected:
  bool drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode) override;

 private:
  std::vector<Rect> dirtyRects = {};
  std::vector<float> dirtyAreas = {};
  Color _backgroundColor = Color::Transparent();

  bool mergeDirtyList(bool forceMerge);

  friend class DisplayList;
};
}  // namespace tgfx
