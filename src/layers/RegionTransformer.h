/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Rect.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {
/**
 * The RegionTransformer class is used to transform a rectangle region.
 */
class RegionTransformer {
 public:
  /**
   * Creates a RegionTransformer that clips the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromClip(
      const Rect& clipRect, std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies filter transformations to the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromFilters(
      const std::vector<std::shared_ptr<LayerFilter>>& filters, float contentScale,
      std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies style transformations to the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromStyles(
      const std::vector<std::shared_ptr<LayerStyle>>& styles, float contentScale,
      std::shared_ptr<RegionTransformer> outer = nullptr);

  explicit RegionTransformer(std::shared_ptr<RegionTransformer> outer);

  virtual ~RegionTransformer() = default;

  /**
   * Transforms the given rectangle using the transformation defined by this object.
   */
  void transform(Rect* bounds) const;

 protected:
  virtual void onTransform(Rect* bounds) const = 0;

  virtual bool isClip() const {
    return false;
  }

 private:
  std::shared_ptr<RegionTransformer> outer = nullptr;
};
}  // namespace tgfx
