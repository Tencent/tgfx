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

#include "tgfx/core/ImageFilter.h"

namespace tgfx {
/**
 * LayerFilter represents a filter that applies effects to a layer, such as blurs, shadows, or color
 * adjustments. LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter {
 public:
  /**
   * Wraps an ImageFilter in a LayerFilter.
   */
  static std::shared_ptr<LayerFilter> Wrap(const std::shared_ptr<ImageFilter>& filter);

  virtual ~LayerFilter() = default;

  /**
   * Returns the ImageFilter that represents the current state of this LayerFilter.
   */
  virtual std::shared_ptr<ImageFilter> getImageFilter() const = 0;

 protected:
  /**
   * Invalidates the filter, causing it to be re-computed the next time it is requested.
   */
  void invalidate();
};
}  // namespace tgfx
