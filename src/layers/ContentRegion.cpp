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

#include "layers/ContentRegion.h"
#include "layers/RegionTransformer.h"
#include "layers/contents/LayerContent.h"

namespace tgfx {

std::unique_ptr<ContentRegion> ContentRegion::Make(LayerContent* content,
                                                   RegionTransformer* transformer) {
  if (content == nullptr) {
    return nullptr;
  }
  auto region = std::unique_ptr<ContentRegion>(new ContentRegion());
  region->globalBounds = content->getBounds();
  if (transformer) {
    transformer->transform(&region->globalBounds);
  }
  auto localCoverRect = content->getCoverRect();
  if (!localCoverRect.isEmpty()) {
    auto totalMatrix = Matrix::I();
    if (transformer) {
      transformer->getTotalMatrix(&totalMatrix);
    }
    Matrix inverse = {};
    if (totalMatrix.invert(&inverse)) {
      region->coverRect = localCoverRect;
      region->inverseMatrix = inverse;
    }
  }
  return region;
}

bool ContentRegion::contains(const Rect& globalRect) const {
  if (coverRect.isEmpty()) {
    return false;
  }
  auto localRect = inverseMatrix.mapRect(globalRect);
  return coverRect.contains(localRect);
}

}  // namespace tgfx
