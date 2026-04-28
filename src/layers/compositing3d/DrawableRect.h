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
#include <optional>
#include <vector>
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Point.h"

namespace tgfx {

class LayerStyle;

/**
 * PaintOrder describes a hierarchical paint order. Smaller values are painted first (at bottom).
 * `depth` identifies the hierarchy level (smaller = closer to root), and `sequenceIndex` orders
 * entries within the same depth.
 */
struct PaintOrder {
  int depth = 0;
  int sequenceIndex = 0;

  bool operator<(const PaintOrder& other) const {
    if (depth != other.depth) {
      return depth < other.depth;
    }
    return sequenceIndex < other.sequenceIndex;
  }

  bool operator>=(const PaintOrder& other) const {
    return !(*this < other);
  }
};

/**
 * DrawableRectBackground groups the data required to apply background-sourced layer styles to a
 * DrawableRect. The region is defined by mask's alpha, with mask's top-left corner placed at
 * maskOffset in the rect local space.
 */
struct DrawableRectBackground {
  // Background styles applied to the masked region. Guaranteed non-empty, and every entry's
  // extraSourceType() is LayerStyleExtraSourceType::Background.
  std::vector<std::shared_ptr<LayerStyle>> styles = {};
  // The alpha mask defining the region to which styles apply. Guaranteed non-null.
  std::shared_ptr<Image> mask = nullptr;
  // The offset of mask's top-left corner in the rect local space.
  Point maskOffset = {};
  // The 2D transformation from the rect local space to the global DisplayList space.
  Matrix globalMatrix = Matrix::I();
};

/**
 * DrawableRect describes a rectangle to be drawn with a 3D transformation, filled by an image
 * texture. The rect has the same size as the image (image.width x image.height). Its local 2D
 * coordinate system (the "rect local space") has its origin at the rect's top-left corner, and
 * `matrix` transforms this space to the render target space.
 */
struct DrawableRect {
  // The image filling the rect.
  std::shared_ptr<Image> image = nullptr;
  // The 3D transformation matrix from the rect local space to the render target space.
  Matrix3D matrix = {};
  // Whether to enable edge antialiasing.
  bool antiAlias = true;
  // The paint order of this rect.
  PaintOrder paintOrder = {};
  // Background data for the rect. std::nullopt if no background styles apply.
  std::optional<DrawableRectBackground> background = std::nullopt;
};

}  // namespace tgfx
