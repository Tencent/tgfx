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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "tgfx/core/Path.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {

/**
 * PathMask clips a layer's content with a vector path. A layer can hold multiple path masks,
 * which are combined in order using each mask's boolean operation to produce the final visible
 * region of the content. Path masks are applied before the layer-level mask (see
 * Layer::setMask()).
 *
 * Combination rules:
 * - Masks with an empty path are skipped. The first effective mask initializes the combined
 *   region with its own path (its operation is ignored), and each subsequent mask combines its
 *   path into the region using its operation.
 * - If the combined region is empty, the entire layer is hidden. Note this differs from PAG,
 *   which treats an empty combined mask path as no mask and keeps the layer fully visible.
 * - If all masks have zero feather and full opacity, the combined region is applied as a clip
 *   path and opacity has no effect. Otherwise, boolean operations are ignored and each mask is
 *   drawn with its own opacity, blurred by feather / 2, and composited in SrcOver order.
 * - Expansion strokes the path outline by |expansion| * 2 and unions (positive) or subtracts
 *   (negative) the stroked outline from the original path.
 *
 * PathMasks are mutable and can be changed at any time.
 */
class PathMask : public LayerProperty {
 public:
  /**
   * Creates an empty path mask with default values: Union operation, not inverted, zero feather,
   * full opacity, and zero expansion.
   */
  static std::shared_ptr<PathMask> Make();

  /**
   * Returns the path of the mask, in the coordinate space of the owning layer. The default value
   * is an empty path.
   */
  const Path& path() const {
    return _path;
  }

  /**
   * Sets the path of the mask.
   */
  void setPath(const Path& path);

  /**
   * Returns the boolean operation used to combine this mask's path into the accumulated region of
   * the owning layer. Only PathOp::Difference, PathOp::Intersect, PathOp::Union, and PathOp::XOR
   * are supported; PathOp::Append and PathOp::Extend are treated as PathOp::Union. The default
   * value is PathOp::Union.
   */
  PathOp op() const {
    return _op;
  }

  /**
   * Sets the boolean operation used to combine this mask's path.
   */
  void setOp(PathOp op);

  /**
   * Returns true if the mask region is inverted, making the area outside the path visible instead
   * of the area inside. If the mask is the first effective mask of the owning layer and its
   * operation is PathOp::Difference, this flag is flipped. The default value is false.
   */
  bool inverted() const {
    return _inverted;
  }

  /**
   * Sets whether the mask region is inverted.
   */
  void setInverted(bool value);

  /**
   * Returns the feather radius of the mask along the x and y axes. If any mask of the owning
   * layer has a non-zero feather, all masks of that layer switch to the feathered compositing
   * path described above. The default value is {0, 0}.
   */
  const Point& feather() const {
    return _feather;
  }

  /**
   * Sets the feather radius of the mask along the x and y axes.
   */
  void setFeather(const Point& value);

  /**
   * Returns the opacity of the mask, ranging from 0.0 (fully transparent) to 1.0 (fully opaque).
   * Opacity only takes effect on the feathered compositing path. The default value is 1.0.
   */
  float opacity() const {
    return _opacity;
  }

  /**
   * Sets the opacity of the mask.
   */
  void setOpacity(float value);

  /**
   * Returns the expansion of the mask path in pixels. Positive values expand the path, negative
   * values shrink it. The default value is 0.
   */
  float expansion() const {
    return _expansion;
  }

  /**
   * Sets the expansion of the mask path in pixels.
   */
  void setExpansion(float value);

 private:
  PathMask() = default;

  Path _path = {};
  PathOp _op = PathOp::Union;
  bool _inverted = false;
  Point _feather = {};
  float _opacity = 1.0f;
  float _expansion = 0.0f;
};

}  // namespace tgfx
