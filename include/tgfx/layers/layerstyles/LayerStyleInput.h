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
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * Contains the source data that a LayerStyle needs to perform its drawing.
 */
struct LayerStyleInput {
  /**
   * The opaque layer content image. Rendered with normal fills, then all semi-transparent pixels
   * are converted to fully opaque (fully transparent pixels are preserved).
   */
  std::shared_ptr<Image> content = nullptr;

  /**
   * The offset of the content image's top-left corner in the layer's local coordinate space.
   * Styles that need a stable sampling origin use this to anchor their pattern.
   */
  Point contentOffset = {};

  /**
   * The scale factor of the layer content relative to its original size. Some layer styles have
   * size-related parameters that must be adjusted with this scale factor.
   */
  float contentScale = 1.0f;

  /**
   * Optional extra source image. The semantics depend on the LayerStyle's extraSourceType: for
   * Contour styles it is similar to content, but includes geometries from alpha=0 painters and
   * replaces gradient fills with solid colors; for Background styles it is the normally rendered
   * content below the current layer; for None styles it is nullptr.
   */
  std::shared_ptr<Image> extraSource = nullptr;

  /**
   * The offset of the extra source image relative to the content image. Only meaningful when
   * extraSource is non-null.
   */
  Point extraSourceOffset = {};

  /**
   * Optional content shape of the layer. For styles whose needContentShape() returns true, it is
   * the layer's vector shape (e.g. Rect, Oval, or RRect with fill/stroke info) when extractable;
   * otherwise the layer's bounding rect is used as a fallback. For styles that do not need it,
   * this is std::nullopt.
   */
  std::optional<StyledShape> contentShape = std::nullopt;
};

}  // namespace tgfx
