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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class LayerContent;
class RegionTransformer;

class ContentRegion {
 public:
  // Creates a ContentRegion from the given content and transformer. Returns nullptr if content is
  // nullptr.
  static std::unique_ptr<ContentRegion> Make(LayerContent* content, RegionTransformer* transformer);

  // The content bounds in global coordinates.
  const Rect& bounds() const {
    return globalBounds;
  }

  // Returns true if the given global rect is fully contained within the cover region of the
  // content. Always returns false if the content type does not have a well-defined cover region.
  bool contains(const Rect& globalRect) const;

 private:
  Rect globalBounds = {};     // in global coordinates (after transformer)
  Rect coverRect = {};        // in local coordinates, empty if no cover region
  Matrix inverseMatrix = {};  // global-to-local inverse matrix, only valid when coverRect is set
};

}  // namespace tgfx
