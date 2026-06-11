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
#include "tgfx/core/Image.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

class SpreadUtils {
 public:
  struct OffsetImage {
    std::shared_ptr<Image> image = nullptr;
    /**
     * The offset of the image relative to the content image, scaled by contentScale.
     */
    Point offset = {};
  };

  /**
   * Rasterizes the contentShape with spread applied into a tightly-sized alpha image. Positive
   * spread outsets the shape, negative spread insets it. Returns {nullptr, {}} when contentShape is
   * unavailable or the path is empty.
   */
  static OffsetImage MakeSpreadShapeImage(const LayerStyleInput& input, float spread);
};

}  // namespace tgfx
