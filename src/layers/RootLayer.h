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
  static std::shared_ptr<RootLayer> Make(DisplayList* displayList);

  ~RootLayer() override;

  /**
   * Invalidates a specific rectangle in the root layer. This method is used to mark a portion of
   * the layer tree as needing to be redrawn.
   */
  void invalidateRect(const Rect& rect);

  /**
   * Returns true if any existing dirty rectangle overlaps the given drawRect for the specified
   * LayerStyle, and applies LayerStyle::filterBackground() to the dirty rectangles.
   */
  bool invalidateBackground(const Rect& drawRect, LayerStyle* layerStyle, float contentScale);

  /**
   * Returns true if there are any dirty rectangles in the root layer.
   */
  bool hasDirtyRegions() const {
    return !dirtyRects.empty();
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
  void drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                 const Matrix3D* transform = nullptr) override;

 private:
  RootLayer() = default;

  std::vector<Rect> dirtyRects = {};
  std::vector<float> dirtyAreas = {};
  Color _backgroundColor = Color::Transparent();

  bool mergeDirtyList(bool forceMerge);

  friend class DisplayList;
};
}  // namespace tgfx
